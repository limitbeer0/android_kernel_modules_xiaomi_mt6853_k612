#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define CONFIG_OF 1
#include "camera_isp.h"
#include <linux/cannon_ion.h>

static unsigned count(void)
{
	unsigned n = ~0U;
	FILE *f = fopen("/sys/module/camera_isp/parameters/ion_kept_buffers", "r");
	if (f) { if (fscanf(f, "%u", &n) != 1) n = ~0U; fclose(f); }
	return n;
}
static void check(int ok, const char *name)
{
	printf("%s: %s (kept=%u, errno=%d)\n", name, ok ? "PASS" : "FAIL", count(), errno);
	if (!ok) exit(1);
}
int main(void)
{
	struct cannon_ion_allocation alloc = { .len = 4096, .align = 4096, .heap_id_mask = 1 };
	struct cannon_ion_fd shared = {0};
	struct cannon_ion_handle handle;
	struct ISP_DEV_ION_NODE_STRUCT node = { .devNode = ISP_CAM_A_IDX, .dmaPort = 0, .size = 4096 };
	unsigned hw = ISP_CAM_A_IDX, baseline;
	int ion = open("/dev/ion", O_RDWR), isp;
	alarm(15);
	check(ion >= 0, "open ION");
	check(!ioctl(ion, CION_ALLOC, &alloc), "alloc real DMA-BUF");
	shared.handle = alloc.handle;
	check(!ioctl(ion, CION_SHARE, &shared), "share fd");
	handle.handle = alloc.handle;
	check(!ioctl(ion, CION_FREE, &handle), "release ION handle");
	close(ion);
	isp = open("/dev/camera-isp", O_RDWR);
	check(isp >= 0, "open ISP");
	baseline = count();
	check(baseline == 0, "empty baseline");
	node.memID = shared.fd;
	check(!ioctl(isp, ISP_ION_IMPORT, &node) && count() == 1, "import");
	check(!ioctl(isp, ISP_ION_IMPORT, &node) && count() == 1, "duplicate import");
	check(!ioctl(isp, ISP_ION_FREE, &node) && count() == 1, "first free retains reference");
	check(!ioctl(isp, ISP_ION_FREE, &node) && count() == 0, "second free drops reference");
	check(ioctl(isp, ISP_ION_FREE, &node) == -1 && errno == ENOENT, "missing free rejected");
	check(!ioctl(isp, ISP_ION_IMPORT, &node) && count() == 1, "reimport");
	check(!ioctl(isp, ISP_ION_FREE_BY_HWMODULE, &hw) && count() == 0, "free by HW module");
	node.devNode = ISP_CAM_C_IDX;
	check(ioctl(isp, ISP_ION_IMPORT, &node) == -1 && errno == EINVAL, "nonexistent RAW C rejected");
	node.devNode = ISP_CAM_A_IDX;
	node.memID = -1;
	check(ioctl(isp, ISP_ION_IMPORT, &node) == -1 && errno == EINVAL, "invalid fd rejected");
	node.memID = shared.fd;
	check(!ioctl(isp, ISP_ION_IMPORT, &node), "retain for close test");
	close(shared.fd);
	check(count() == 1, "ISP retains buffer after shared fd closes");
	close(isp);
	check(count() == 0, "last ISP close releases remaining imports");
	return 0;
}
