#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdint.h>
#define CONFIG_OF 1
#include "camera_isp.h"

int main(int argc, char **argv)
{
	struct ISP_CLK_INFO levels = {0};
	struct ISP_GET_CLK_INFO cur = {0};
	unsigned int pwr[2];
	unsigned long clk = 624;
	int fd, ret, i, bad = 0;
	if (argc > 1 && !strcmp(argv[1], "abi")) {
		printf("CLOCKS=%lx CURRENT=%lx POWER=%lx CLK_INFO=%zu GET_CLK=%zu PMQOS=%zu\n",
			(unsigned long)ISP_GET_SUPPORTED_ISP_CLOCKS,
			(unsigned long)ISP_GET_CUR_ISP_CLOCK,
			(unsigned long)ISP_POWER_CTRL, sizeof(levels), sizeof(cur),
			sizeof(struct ISP_PM_QOS_INFO_STRUCT));
		return 0;
	}
	alarm(15);
	fd = open("/dev/camera-isp", O_RDWR);
	if (fd < 0) { perror("open"); return 1; }
	ret = ioctl(fd, ISP_GET_SUPPORTED_ISP_CLOCKS, &levels);
	printf("CLOCKS ret=%d errno=%d levels=%u:", ret, errno, levels.clklevelcnt);
	for (i = 0; i < levels.clklevelcnt && i < ISP_CLK_LEVEL_CNT; i++)
		printf(" %u", levels.clklevel[i]);
	puts("");
	if (ret || levels.clklevelcnt != 1 || levels.clklevel[0] != 624) bad = 1;
	ret = ioctl(fd, ISP_DFS_UPDATE, &clk);
	printf("DFS624 ret=%d errno=%d\n", ret, errno);
	if (ret) bad = 1;
	ret = ioctl(fd, ISP_GET_CUR_ISP_CLOCK, &cur);
	printf("CURRENT ret=%d errno=%d MHz=%u target=%u\n",ret,errno,cur.curClk,cur.targetClk);
	if (ret || cur.curClk != 624 || cur.targetClk != 624) bad = 1;
	if (argc > 1 && !strcmp(argv[1], "power"))
		for (i = ISP_CAM_A_IDX; i <= ISP_CAM_B_IDX; i++) {
			pwr[0] = i; pwr[1] = 1;
			ret = ioctl(fd, ISP_POWER_CTRL, pwr);
			printf("POWER module=%u on ret=%d errno=%d\n", pwr[0],ret,errno);
			if (ret) { bad = 1; break; }
			usleep(100000);
			pwr[1] = 0;
			ret = ioctl(fd, ISP_POWER_CTRL, pwr);
			printf("POWER module=%u off ret=%d errno=%d\n",pwr[0],ret,errno);
			if (ret) { bad = 1; break; }
		}
	close(fd);
	return bad;
}
