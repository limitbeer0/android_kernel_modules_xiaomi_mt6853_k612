/* SPDX-License-Identifier: GPL-2.0-only */
/* Read-only comparison of the stock and bringup SENINF clock ABI. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct isp_clocks {
	unsigned char count;
	unsigned int mhz[10];
};
_Static_assert(sizeof(struct isp_clocks) == 44, "SENINF clock ABI size");
_Static_assert(offsetof(struct isp_clocks, mhz) == 4, "SENINF clock ABI offset");

int main(void)
{
	struct isp_clocks clocks = {0};
	unsigned int current = 0;
	unsigned long supported_cmd = _IOWR('s', 100, struct isp_clocks);
	unsigned long current_cmd = _IOWR('s', 105, unsigned int);
	int fd = open("/dev/seninf", O_RDWR | O_CLOEXEC);
	int ret, failed = 0;

	if (fd < 0) {
		fprintf(stderr, "open /dev/seninf: %s\n", strerror(errno));
		return 1;
	}
	errno = 0;
	ret = ioctl(fd, supported_cmd, &clocks);
	printf("supported cmd=%#lx ret=%d errno=%d count=%u\n",
	       supported_cmd, ret, errno, clocks.count);
	if (ret < 0 || !clocks.count || clocks.count > 10)
		failed = 1;
	else
		for (unsigned int i = 0; i < clocks.count; i++)
			printf("clock[%u]=%u MHz\n", i, clocks.mhz[i]);
	errno = 0;
	ret = ioctl(fd, current_cmd, &current);
	printf("current cmd=%#lx ret=%d errno=%d value=%u MHz\n",
	       current_cmd, ret, errno, current);
	if (ret < 0)
		failed = 1;
	close(fd);
	return failed;
}
