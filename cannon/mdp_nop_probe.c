// SPDX-License-Identifier: GPL-2.0-only
/* Exercise real MDP scheduling with a NOP packet; no image DMA is enabled. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "mdp_probe_uapi.h"
_Static_assert(sizeof(struct op_meta)==16,"native meta ABI");
_Static_assert(sizeof(struct mdp_submit)==336,"native submit ABI");
_Static_assert(sizeof(struct mdp_wait)==48,"native wait ABI");
int main(void)
{
	int fd=open("/dev/mtk_mdp",O_RDWR|O_CLOEXEC), ret;
	struct op_meta meta={.op=CMDQ_MOP_NOP};
	struct mdp_submit submit={.metas=(uintptr_t)&meta,.meta_count=1,.engine_flag=1ULL<<8};
	struct mdp_wait wait={0};
	alarm(20);
	if(fd<0) { perror("open MDP");return 1; }
	ret=ioctl(fd,CMDQ_IOCTL_ASYNC_EXEC,&submit);
	printf("MDP NOP submit ret=%d errno=%d job=%#llx\n",ret,errno,(unsigned long long)submit.job_id);
	fflush(stdout);
	if(ret) { close(fd);return 1; }
	wait.job_id=submit.job_id;
	ret=ioctl(fd,CMDQ_IOCTL_ASYNC_WAIT,&wait);
	printf("MDP NOP wait ret=%d errno=%d\n",ret,errno);
	close(fd);
	return ret!=0;
}
