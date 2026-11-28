// SPDX-License-Identifier: GPL-2.0-only
/* Real MDP ioctl and GCE readback DMA-buffer lifecycle; submits no engine DMA. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
struct mdp_readback { uint32_t count, start_id; };
struct mdp_read_readback { uint32_t count; uint64_t ids, ret_values; };
_Static_assert(sizeof(struct mdp_readback)==8, "native readback ABI");
_Static_assert(sizeof(struct mdp_read_readback)==24, "native pointer ABI");
#define QUERY_CAP _IOW('x',10,int)
#define ALLOC_RB _IOW('x',22,struct mdp_readback)
#define FREE_RB _IOW('x',23,struct mdp_readback)
#define READ_RB _IOW('x',24,struct mdp_read_readback)
static void check(int ok, const char *step)
{
	if(!ok) { fprintf(stderr,"FAIL MDP %s errno=%d\n",step,errno);exit(1); }
}
int main(void)
{
	int fd=open("/dev/mtk_mdp",O_RDWR|O_CLOEXEC), caps=0;
	struct mdp_readback rb={.count=4};
	uint32_t ids[2],values[2]={0};
	struct mdp_read_readback read={.count=2,.ids=(uintptr_t)ids,.ret_values=(uintptr_t)values};
	alarm(15);
	check(fd>=0,"open");
	check(!ioctl(fd,QUERY_CAP,&caps) && (caps&1),"real WFE capability");
	errno=0;
	check(ioctl(fd,QUERY_CAP,NULL)<0 && errno==EFAULT,"invalid output pointer");
	check(!ioctl(fd,ALLOC_RB,&rb),"allocate actual GCE readback buffer");
	ids[0]=rb.start_id;ids[1]=rb.start_id+3;
	check(!ioctl(fd,READ_RB,&read),"read allocated slots");
	check(!ioctl(fd,FREE_RB,&rb),"free slots");
	check(ioctl(fd,FREE_RB,&rb)<0,"double free rejected");
	close(fd);
	printf("PASS MDP: caps=%#x readback_id=%#x values=%#x/%#x; alloc/read/free and invalid requests\n",
		caps,rb.start_id,values[0],values[1]);
	return 0;
}
