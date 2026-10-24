/* SPDX-License-Identifier: GPL-2.0-only */
/* Run the same allocation/lifetime checks on Cannon 4.14 and 6.12. */
#define _GNU_SOURCE
#include <linux/cannon_ion.h>
#include <linux/dma-buf.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static int stock_quirks;

static void check(int ok, const char *what)
{
	if (!ok) {
		fprintf(stderr, "FAIL %s: errno=%d (%s)\n", what, errno, strerror(errno));
		exit(1);
	}
}

static int fd_count(void)
{
	DIR *d = opendir("/proc/self/fd");
	struct dirent *entry;
	int count = 0;
	check(d != NULL, "opendir fd");
	while ((entry = readdir(d)))
		if (entry->d_name[0] != '.')
			count++;
	closedir(d);
	return count;
}

static void one_allocation(int ion, unsigned int heap, unsigned int cached)
{
	struct cannon_ion_allocation a = { .len=8192, .align=4096,
		.heap_id_mask=1U << heap, .flags=cached };
	struct cannon_ion_fd shared, imported, duplicate, mapped;
	struct cannon_ion_handle freed;
	struct dma_buf_sync sync = { .flags=DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW };
	unsigned char *p, *q;
	int other, before;

	check(ioctl(ion, CION_ALLOC, &a) == 0, "allocate");
	shared = (struct cannon_ion_fd) { .handle=a.handle, .fd=-1 };
	check(ioctl(ion, CION_SHARE, &shared) == 0, "share");
	p = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, shared.fd, 0);
	check(p != MAP_FAILED, "mmap");
	check(ioctl(shared.fd, DMA_BUF_IOCTL_SYNC, &sync) == 0, "cpu access start");
	for (int i=0; i<8192; i++)
		check(p[i] == 0, "zero-filled allocation");
	for (int i=0; i<8192; i++)
		p[i] = (unsigned char)(i * 13 + heap + cached);
	sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
	check(ioctl(shared.fd, DMA_BUF_IOCTL_SYNC, &sync) == 0, "cpu access end");
	check(ioctl(ion, CION_SYNC, &shared) == 0, "legacy sync");
	other = open("/dev/ion", O_RDWR | O_CLOEXEC);
	check(other >= 0, "second client");
	imported = (struct cannon_ion_fd) { .fd=shared.fd };
	check(ioctl(other, CION_IMPORT, &imported) == 0, "import other client");
	duplicate = (struct cannon_ion_fd) { .fd=shared.fd };
	check(ioctl(other, CION_IMPORT, &duplicate) == 0, "repeated import");
	check(imported.handle == duplicate.handle, "repeated import preserves handle");
	freed.handle = a.handle;
	check(ioctl(ion, CION_FREE, &freed) == 0, "free original handle");
	check(ioctl(ion, CION_FREE, &freed) < 0, "reject stale handle");
	freed.handle = imported.handle;
	check(ioctl(other, CION_FREE, &freed) == 0, "free one import reference");
	mapped = (struct cannon_ion_fd) { .handle=imported.handle, .fd=-1 };
	check(ioctl(other, CION_MAP, &mapped) == 0, "map remaining import reference");
	q = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, mapped.fd, 0);
	check(q != MAP_FAILED, "mmap imported");
	sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;
	check(ioctl(mapped.fd, DMA_BUF_IOCTL_SYNC, &sync) == 0, "import cpu access start");
	for (int i=0; i<8192; i++)
		check(q[i] == (unsigned char)(i * 13 + heap + cached), "shared bytes");
	q[4095] ^= 0x55;
	check(p[4095] == q[4095], "mapping alias");
	sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
	check(ioctl(mapped.fd, DMA_BUF_IOCTL_SYNC, &sync) == 0, "import cpu access end");
	/* A read-only ioctl result must neither install nor leak a descriptor. */
	struct cannon_ion_fd *bad = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
				       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	check(bad != MAP_FAILED, "fault test mapping");
	*bad = mapped;
	check(mprotect(bad, 4096, PROT_READ) == 0, "fault test protect");
	before = fd_count();
	errno = 0;
	check(ioctl(other, CION_SHARE, bad) == -1 && errno == EFAULT, "share output EFAULT");
	/* Old ION has a known fd leak on this error path; report separately. */
	int delta = fd_count()-before;
	printf("heap=%u cached=%u EFAULT_fd_delta=%d\n", heap, cached, delta);
	if (!stock_quirks)
		check(delta == 0, "EFAULT must not leak fd");
	munmap(bad, 4096);
	check(ioctl(other, CION_FREE, &freed) == 0, "free final import");
	close(other);
	close(shared.fd);
	close(mapped.fd);
	/* Existing mappings must survive all handles and descriptors closing. */
	check(p[4095] == q[4095], "mapping survives close");
	munmap(p, 8192);
	munmap(q, 8192);
	printf("PASS heap=%u cached=%u: zero/map/share/import/refcounts/bytes/lifetime\n", heap, cached);
}

int main(int argc, char **argv)
{
	struct cannon_ion_query query = {0};
	struct cannon_ion_heap heaps[32] = {0};
	int ion = open("/dev/ion", O_RDWR | O_CLOEXEC);
	stock_quirks = argc == 2 && !strcmp(argv[1], "--stock-query-quirk");
	check(ion >= 0, "open /dev/ion");
	printf("ABI alloc=%#lx alloc32=%#lx query=%#lx\n",
	       (unsigned long)CION_ALLOC, (unsigned long)CION_ALLOC32, (unsigned long)CION_QUERY);
	check(ioctl(ion, CION_QUERY, &query) == 0, "heap count");
	printf("heap count=%u\n", query.cnt);
	query.cnt = 32; query.heaps = (uintptr_t)heaps;
	int ret = ioctl(ion, CION_QUERY, &query);
	if (stock_quirks && ret == -1 && errno == EINVAL)
		puts("4.14 query quirk: populated list but returned EINVAL (matches source)");
	else
		check(ret == 0, "heap list");
	for (unsigned i=0; i<query.cnt && i<32; i++)
		printf("heap id=%u type=%u name=%.32s\n", heaps[i].heap_id, heaps[i].type, heaps[i].name);
	for (unsigned i=0; i<3; i++)
		for (unsigned cached=0; cached<2; cached++)
			one_allocation(ion, (unsigned[]){0,10,12}[i], cached);
	close(ion);
	puts("PASS all normal heaps");
	return 0;
}
