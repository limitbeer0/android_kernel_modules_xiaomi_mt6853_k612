// SPDX-License-Identifier: GPL-2.0-only
/* Exercise actual 4.14 native custom-ION payloads, including fd-only lifetime. */
#define _GNU_SOURCE
#include <linux/cannon_ion.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static void check(int ok, const char *step)
{
	if (!ok) {
		fprintf(stderr, "FAIL %s: errno=%d (%s)\n", step, errno, strerror(errno));
		exit(1);
	}
}

static int custom(int ion, unsigned int cmd, unsigned char *payload)
{
	struct cannon_ion_custom data = {.cmd=cmd, .arg=(uintptr_t)payload};
	return ioctl(ion, CION_CUSTOM, &data);
}

static void put32(unsigned char *p, uint32_t value) { memcpy(p, &value, 4); }

int main(void)
{
	_Alignas(8) unsigned char sys[1024] = {0}, mm[1024] = {0};
	const char name[] = "cannon-ion-metadata-probe-long-name-0123456789";
	const uint32_t values[] = {0x01234567, 0x89abcdef, 0xdeadbeef, 0xffffffff};
	struct cannon_ion_allocation a = {.len=8192, .align=4096,
		.heap_id_mask=1U<<12};
	struct cannon_ion_fd shared, imported;
	struct cannon_ion_handle freed;
	int ion = open("/dev/ion", O_RDWR | O_CLOEXEC), other;
	check(sizeof(unsigned long) == 8, "native AArch64 probe");
	check(ion >= 0, "open");
	put32(sys, 4); /* ION_SYS_SET_CLIENT_NAME, native union offset 8 */
	strcpy((char *)sys + 8, "cannon-ion-metadata-client");
	check(custom(ion, 0, sys) == 0, "set client name");
	check(ioctl(ion, CION_ALLOC, &a) == 0, "allocate camera heap");
	put32(mm, 1); /* ION_MM_SET_DEBUG_INFO */
	put32(mm+8, a.handle);
	strcpy((char *)mm+16, name);
	memcpy(mm+64, values, sizeof(values));
	check(custom(ion, 1, mm) == 0, "set debug name and all four values");
	shared = (struct cannon_ion_fd){.handle=a.handle, .fd=-1};
	check(ioctl(ion, CION_SHARE, &shared) == 0, "share");
	freed.handle = a.handle;
	check(ioctl(ion, CION_FREE, &freed) == 0, "free only ION handle");
	usleep(600000); /* More than two metadata collection periods; fd survives. */
	other = open("/dev/ion", O_RDWR | O_CLOEXEC);
	check(other >= 0, "second client");
	imported = (struct cannon_ion_fd){.fd=shared.fd};
	check(ioctl(other, CION_IMPORT, &imported) == 0, "import fd-only buffer");
	memset(mm, 0, sizeof(mm));
	put32(mm, 2); /* ION_MM_GET_DEBUG_INFO */
	put32(mm+8, imported.handle);
	check(custom(other, 1, mm) == 0, "get metadata from imported handle");
	check(!strcmp((char *)mm+16, name), "full 48-byte ABI debug name");
	check(!memcmp(mm+64, values, sizeof(values)), "metadata survives fd-only interval");
	put32(mm+8, 0);
	errno=0;
	check(custom(other, 1, mm) == -1 && errno == EINVAL, "reject invalid handle");
	put32(mm+8, imported.handle);
	unsigned char *bad=mmap(NULL, 4096, PROT_READ|PROT_WRITE,
			       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	check(bad != MAP_FAILED, "fault mapping");
	memcpy(bad, mm, sizeof(mm));
	check(mprotect(bad, 4096, PROT_READ) == 0, "protect output");
	errno=0;
	check(custom(other, 1, bad) == -1 && errno == EFAULT, "GET copy-out fault");
	munmap(bad, 4096);
	put32(mm, 1);
	strcpy((char *)mm+16, "last-name");
	check(custom(other, 1, mm) == 0, "update metadata");
	put32(mm, 2);
	memset(mm+16, 0, 64);
	check(custom(other, 1, mm) == 0, "get updated metadata");
	check(!strcmp((char *)mm+16, "last-name"), "name updated");
	check(!memcmp(mm+64, values, sizeof(values)), "four values updated");
	/* A full non-NUL input is bounded to the original 47-character name. */
	put32(mm, 1);
	memset(mm+16, 'x', 48);
	check(custom(other, 1, mm) == 0, "non-NUL debug name");
	put32(mm, 2);
	memset(mm+16, 0, 64);
	check(custom(other, 1, mm) == 0 && mm[63] == 0 &&
	      strlen((char *)mm+16) == 47, "bounded debug name");
	freed.handle=imported.handle;
	check(ioctl(other, CION_FREE, &freed) == 0, "free import");
	close(shared.fd);
	close(other);
	close(ion);
	usleep(600000); /* Let the metadata-only reference be reclaimed. */
	puts("PASS native ION: client name, debug values, cross-client/fd-only lifetime, bounds, faults");
	return 0;
}
