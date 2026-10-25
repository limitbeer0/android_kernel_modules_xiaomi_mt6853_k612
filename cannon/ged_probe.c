// SPDX-License-Identifier: GPL-2.0
/* Exercise the real gralloc_extra fd ABI; no GPU jobs or physical MMIO. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Layouts from GED's ged_bridge_id.h, shared by the old and new clients. */
struct bridge_package {
	uint32_t function_id;
	int32_t size;
	void *input;
	int32_t input_size;
	void *output;
	int32_t output_size;
};
struct alloc_in { int32_t count; uint32_t sizes[22]; };
struct alloc_out { int32_t fd, error; };
struct get_in { int32_t fd, region, offset, count; };
struct get_out { int32_t error; uint32_t words[3]; };
struct set_in { int32_t fd, region, offset, count; uint32_t words[3]; };
struct set_out { int32_t error; };

_Static_assert(sizeof(struct alloc_out) == 8, "GE_ALLOC output ABI");
_Static_assert(sizeof(struct get_in) == 16, "GE_GET input ABI");
_Static_assert(sizeof(struct set_in) == 28, "GE_SET input ABI");

static void require(int condition, const char *what)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s (errno=%d: %s)\n", what, errno, strerror(errno));
		exit(1);
	}
}

static int bridge(int ged, unsigned int id, void *input, int input_size,
		  void *output, int output_size)
{
	unsigned long command = _IOWR('g', id, struct bridge_package);
	struct bridge_package package = {
		.function_id = command, .size = sizeof(package),
		.input = input, .input_size = input_size,
		.output = output, .output_size = output_size,
	};

	return ioctl(ged, command, &package);
}

static int fd_count(void)
{
	DIR *dir = opendir("/proc/self/fd");
	struct dirent *entry;
	int count = 0;

	require(dir != NULL, "open fd accounting directory");
	while ((entry = readdir(dir)))
		if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
			count++;
	closedir(dir);
	return count;
}

static struct alloc_in allocation(int count)
{
	struct alloc_in input = {.count = count};
	int i;

	for (i = 0; i < 22; i++)
		input.sizes[i] = 64;
	return input;
}

static void read_words(int ged, int fd, int region, const uint32_t expected[3])
{
	struct get_in input = {fd, region, 1, 3};
	struct get_out output = {.error = -1, .words = {~0U, ~0U, ~0U}};

	require(bridge(ged, 101, &input, sizeof(input), &output, sizeof(output)) == 0,
		"GE_GET ioctl");
	require(output.error == 0, "GE_GET result");
	require(!memcmp(output.words, expected, sizeof(output.words)), "GE_GET real contents");
}

static void positive(int ged, int count, int expected_fd)
{
	static const uint32_t zeros[3];
	static const uint32_t values[3] = {0x12345678, 0xabcdef01, 0x55aa77cc};
	struct alloc_in input = allocation(count);
	struct alloc_out output = {-1, -1};
	struct set_in set = {.region = count - 1, .offset = 1, .count = 3};
	struct set_out result = {-1};
	int copy, before = fd_count();

	require(bridge(ged, 100, &input, 4 + count * 4, &output, sizeof(output)) == 0,
		"GE_ALLOC ioctl");
	require(output.fd >= 0 && output.error == 0, "GE_ALLOC real fd");
	if (expected_fd >= 0)
		require(output.fd == expected_fd, "fd zero is a successful allocation");
	require(fcntl(output.fd, F_GETFD) == FD_CLOEXEC, "metadata fd CLOEXEC");
	read_words(ged, output.fd, count - 1, zeros);
	set.fd = output.fd;
	memcpy(set.words, values, sizeof(values));
	require(bridge(ged, 102, &set, sizeof(set), &result, sizeof(result)) == 0,
		"GE_SET ioctl");
	require(result.error == 0, "GE_SET result");
	copy = dup(output.fd);
	require(copy >= 0, "duplicate metadata fd");
	require(close(output.fd) == 0, "close original metadata fd");
	read_words(ged, copy, count - 1, values);
	require(close(copy) == 0, "close shared metadata fd");
	require(fd_count() == before, "successful allocation has no fd leak");
	printf("PASS: %d regions, zero/read/write/shared-fd lifetime%s\n", count,
		expected_fd == 0 ? ", fd=0 accepted" : "");
}

static void rejected(int ged, struct alloc_in *input, int input_size,
		     int output_size, int bad_pointer, const char *name)
{
	struct { struct alloc_out result; uint32_t padding; } buffer = {{-1, -1}, 0};
	struct alloc_out *output = &buffer.result;
	int before = fd_count(), saved_errno, ret;

	errno = 0;
	ret = bridge(ged, 100, input, input_size,
		bad_pointer ? (void *)(uintptr_t)1 : output, output_size);
	saved_errno = errno;
	if (!bad_pointer && ret == 0 && output->fd >= 0) {
		close(output->fd);
		require(0, "malformed allocation unexpectedly succeeded");
	}
	require(ret < 0 || (!bad_pointer && output->fd < 0 && output->error != 0), name);
	if (bad_pointer)
		require(ret == -1 && saved_errno == EFAULT, "bad output pointer returns EFAULT");
	require(fd_count() == before, "rejected allocation has no fd leak");
	printf("PASS: reject %s, no fd leak\n", name);
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/proc/ged";
	struct alloc_in input;
	int ged = open(path, O_RDWR | O_CLOEXEC);

	require(ged >= 0, "open GED bridge");
	positive(ged, 18, -1);
	positive(ged, 22, -1);
	input = allocation(18);
	rejected(ged, &input, 75, 8, 0, "truncated 18-region input");
	rejected(ged, &input, 92, 8, 0, "18-region count with 22-region byte length");
	input = allocation(22);
	rejected(ged, &input, 76, 8, 0, "22-region count with 18-region byte length");
	input = allocation(17);
	rejected(ged, &input, 76, 8, 0, "unsupported region count");
	input = allocation(18);
	rejected(ged, &input, 76, 4, 0, "short allocation output");
	rejected(ged, &input, 76, 12, 0, "oversized allocation output");
	input.sizes[17] = 0;
	rejected(ged, &input, 76, 8, 0, "zero region size");
	input.sizes[17] = 8193;
	rejected(ged, &input, 76, 8, 0, "oversized region");
	input = allocation(18);
	rejected(ged, &input, 76, 8, 1, "undeliverable metadata fd");
	/* stdin is unused by this diagnostic. Its vacant slot must be valid. */
	if (ged != STDIN_FILENO) {
		close(STDIN_FILENO);
		positive(ged, 18, STDIN_FILENO);
	}
	require(close(ged) == 0, "close GED bridge");
	puts("PASS: real GED gralloc_extra 18/22-region ABI");
	return 0;
}
