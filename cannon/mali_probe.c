/* SPDX-License-Identifier: GPL-2.0-only */
/* Minimal r32p1 JM UAPI/context probe; never submits a userspace GPU job. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define MALI_USE_CSF 0
#include <gpu/arm/midgard/mali_base_kernel.h>
#include <gpu/arm/midgard/mali_kbase_ioctl.h>

_Static_assert(sizeof(struct kbase_ioctl_version_check) == 4, "version ABI");
_Static_assert(sizeof(struct kbase_ioctl_set_flags) == 4, "context ABI");
_Static_assert(sizeof(struct kbase_ioctl_get_gpuprops) == 16, "properties ABI");
_Static_assert(sizeof(union kbase_ioctl_mem_alloc) == 32, "allocation ABI");
_Static_assert(sizeof(struct kbase_ioctl_mem_free) == 8, "free ABI");

static int failure(const char *stage, int error)
{
	fprintf(stderr, "FAIL %s: errno=%d (%s)\n", stage, error, strerror(error));
	return 1;
}

static uint64_t read_le(const unsigned char *p, size_t size)
{
	uint64_t value = 0;
	size_t i;

	for (i = 0; i < size; i++)
		value |= (uint64_t)p[i] << (i * 8);
	return value;
}

static int print_properties(const unsigned char *buffer, size_t size)
{
	size_t offset = 0, count = 0;

	while (offset < size) {
		uint32_t key, property;
		uint64_t value;
		size_t width;
		const char *name = NULL;

		if (size - offset < sizeof(key))
			return failure("GET_GPUPROPS truncated key", EPROTO);
		key = (uint32_t)read_le(buffer + offset, sizeof(key));
		offset += sizeof(key);
		width = (size_t)1 << (key & 3);
		property = key >> 2;
		if (width > size - offset)
			return failure("GET_GPUPROPS truncated value", EPROTO);
		value = read_le(buffer + offset, width);
		offset += width;
		count++;

		switch (property) {
		case KBASE_GPUPROP_PRODUCT_ID: name = "PRODUCT_ID"; break;
		case KBASE_GPUPROP_RAW_GPU_ID: name = "RAW_GPU_ID"; break;
		case KBASE_GPUPROP_MAJOR_REVISION: name = "MAJOR_REVISION"; break;
		case KBASE_GPUPROP_MINOR_REVISION: name = "MINOR_REVISION"; break;
		default: break;
		}
		if (name)
			printf("  %s=0x%" PRIx64 "\n", name, value);
	}
	printf("OK GET_GPUPROPS: %zu bytes, %zu records parsed\n", size, count);
	return 0;
}

int main(int argc, char **argv)
{
	const char *device = "/dev/mali0";
	bool allocate = false, device_set = false;
	struct kbase_ioctl_version_check version = {
		.major = BASE_UK_VERSION_MAJOR,
		.minor = BASE_UK_VERSION_MINOR,
	};
	struct kbase_ioctl_set_flags context = { .create_flags = 0 };
	struct kbase_ioctl_get_gpuprops properties = { 0 };
	struct kbase_ioctl_mem_free release = { 0 };
	unsigned char *buffer = NULL;
	void *tracking = MAP_FAILED;
	long page_size = 0;
	int fd = -1, result = 1, needed, ret, i;

	setvbuf(stdout, NULL, _IONBF, 0);
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--alloc")) {
			allocate = true;
		} else if (argv[i][0] != '-' && !device_set) {
			device = argv[i];
			device_set = true;
		} else {
			fprintf(stderr, "Usage: %s [--alloc] [/dev/mali0]\n", argv[0]);
			return 2;
		}
	}

	printf("TRY open %s (may load device-wide workaround firmware)\n", device);
	fd = open(device, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		failure("open", errno);
		goto out;
	}
	printf("OK open\nTRY VERSION_CHECK JM %u.%u\n", version.major, version.minor);
	ret = ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &version);
	if (ret != 0) {
		failure("VERSION_CHECK", ret < 0 ? errno : EPROTO);
		goto out;
	}
	printf("OK VERSION_CHECK: negotiated %u.%u\n", version.major, version.minor);
	if (version.major != BASE_UK_VERSION_MAJOR || version.minor > BASE_UK_VERSION_MINOR) {
		failure("incompatible JM API version", EPROTONOSUPPORT);
		goto out;
	}

	printf("TRY SET_FLAGS create_flags=0 (create context)\n");
	ret = ioctl(fd, KBASE_IOCTL_SET_FLAGS, &context);
	if (ret != 0) {
		failure("SET_FLAGS/context creation", ret < 0 ? errno : EPROTO);
		goto out;
	}
	printf("OK SET_FLAGS/context creation\nTRY GET_GPUPROPS size\n");
	needed = ioctl(fd, KBASE_IOCTL_GET_GPUPROPS, &properties);
	if (needed <= 0) {
		failure("GET_GPUPROPS size", needed < 0 ? errno : EPROTO);
		goto out;
	}
	if (needed > 1024 * 1024) {
		failure("GET_GPUPROPS size exceeds 1 MiB limit", EOVERFLOW);
		goto out;
	}
	buffer = calloc(1, (size_t)needed);
	if (!buffer) {
		failure("allocate properties buffer", errno);
		goto out;
	}
	properties.buffer = (uint64_t)(uintptr_t)buffer;
	properties.size = (uint32_t)needed;
	printf("OK GET_GPUPROPS size=%d\nTRY GET_GPUPROPS data\n", needed);
	ret = ioctl(fd, KBASE_IOCTL_GET_GPUPROPS, &properties);
	if (ret != needed) {
		failure("GET_GPUPROPS data/length", ret < 0 ? errno : EPROTO);
		goto out;
	}
	if (print_properties(buffer, (size_t)ret))
		goto out;

	if (allocate) {
		union kbase_ioctl_mem_alloc allocation = {
			.in = {
				.va_pages = 1,
				.commit_pages = 1,
				.extension = 0,
				.flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
					 BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR,
			},
		};

		page_size = sysconf(_SC_PAGESIZE);
		if (page_size != 4096) {
			failure("memory probe requires Cannon 4 KiB pages", EINVAL);
			goto out;
		}
		printf("TRY tracking mmap (PROT_NONE, one page)\n");
		tracking = mmap(NULL, (size_t)page_size, PROT_NONE, MAP_SHARED,
				fd, (off_t)BASE_MEM_MAP_TRACKING_HANDLE);
		if (tracking == MAP_FAILED) {
			failure("tracking mmap", errno);
			goto out;
		}
		printf("OK tracking mmap\nTRY MEM_ALLOC one committed page\n");
		ret = ioctl(fd, KBASE_IOCTL_MEM_ALLOC, &allocation);
		if (ret != 0) {
			failure("MEM_ALLOC", ret < 0 ? errno : EPROTO);
			goto out;
		}
		release.gpu_addr = allocation.out.gpu_va;
		if (!release.gpu_addr) {
			failure("MEM_ALLOC returned zero handle", EPROTO);
			goto out;
		}
		printf("OK MEM_ALLOC handle=0x%" PRIx64 " flags=0x%" PRIx64 "\n",
			(uint64_t)allocation.out.gpu_va, (uint64_t)allocation.out.flags);
		/* An unmapped SAME_VA cookie may be released with MEM_FREE.
		 * Do not mmap it: a mapped SAME_VA allocation must use munmap.
		 */
		printf("TRY MEM_FREE\n");
		ret = ioctl(fd, KBASE_IOCTL_MEM_FREE, &release);
		if (ret != 0) {
			failure("MEM_FREE", ret < 0 ? errno : EPROTO);
			goto out;
		}
		release.gpu_addr = 0;
		printf("OK MEM_FREE\n");
	}
	result = 0;
out:
	free(buffer);
	if (release.gpu_addr) {
		ret = ioctl(fd, KBASE_IOCTL_MEM_FREE, &release);
		if (ret != 0)
			result = failure("cleanup MEM_FREE (close will release context)",
					 ret < 0 ? errno : EPROTO);
	}
	if (tracking != MAP_FAILED && munmap(tracking, (size_t)page_size) < 0)
		result = failure("cleanup tracking munmap", errno);
	if (fd >= 0 && close(fd) < 0)
		result = failure("close/context cleanup", errno);
	if (!result)
		printf("PASS JM ABI/context/properties%s; userspace submitted no GPU jobs\n",
			allocate ? "/one-page allocation/free" : "");
	return result;
}
