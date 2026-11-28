// SPDX-License-Identifier: GPL-2.0-only
/* Actual DIP power and DMA-BUF lifetime checks, without starting an image DMA. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
struct allocation { uint64_t length, align; uint32_t heaps, flags; int32_t handle; };
struct share { int32_t handle, fd; };
struct dip_buffer { uint32_t fd, offset, address, check; };
struct dip_mem { uint32_t command, address; uintptr_t va; uint32_t size_diff; };
#define ALLOC _IOWR('I',0,struct allocation)
#define FREE _IOWR('I',1,int32_t)
#define SHARE _IOWR('I',4,struct share)
#define SET_BUF _IOWR('D',16,struct dip_buffer)
#define DET_BUF _IOWR('D',17,uint32_t)
#define SET_MEM _IOWR('D',14,struct dip_mem)
static void check(int ok,const char *step)
{
	if (!ok) { fprintf(stderr,"FAIL DIP %s errno=%d\n",step,errno); exit(1); }
}
static unsigned int count(void)
{
	unsigned int n=~0U;
	FILE *f=fopen("/sys/module/camera_dip_isp6s/parameters/mapped_buffers","r");
	check(f!=NULL,"read mapping count");check(fscanf(f,"%u",&n)==1,"mapping count");fclose(f);
	return n;
}
int main(void)
{
	int dip, second, ion, copy;
	uint32_t fd;
	unsigned int initial;
	struct allocation allocation={.length=8192,.align=4096,.heaps=1U<<10};
	struct share share;
	struct dip_buffer b={0};
	struct dip_mem mem;
	void *va;
	alarm(20);
	dip=open("/dev/camera-dip",O_RDWR|O_CLOEXEC);
	check(dip>=0,"first open enables actual clocks");
	second=open("/dev/camera-dip",O_RDWR|O_CLOEXEC);
	check(second>=0,"second open");
	initial=count();
	ion=open("/dev/ion",O_RDWR|O_CLOEXEC);check(ion>=0,"ION open");
	check(!ioctl(ion,ALLOC,&allocation),"MM heap allocation");
	share.handle=allocation.handle;share.fd=-1;
	check(!ioctl(ion,SHARE,&share),"share actual DMA-BUF");
	check(!ioctl(ion,FREE,&allocation.handle),"free client handle");
	va=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_SHARED,share.fd,0);
	check(va!=MAP_FAILED,"map buffer");
	b.fd=share.fd;
	check(!ioctl(dip,SET_BUF,&b) && count()==initial+1,"persistent CAM IOVA mapping");
	printf("DIP actual low IOVA=%#x\n",b.address);
	b.check=1;
	check(!ioctl(second,SET_BUF,&b) && count()==initial+1,"cross-file lookup shares mapping");
	mem=(struct dip_mem){.command=1,.address=b.address,.va=(uintptr_t)va};
	check(!ioctl(dip,SET_MEM,&mem),"tpipe memory info");
	mem.command=2;
	check(!ioctl(dip,SET_MEM,&mem),"command memory info");
	copy=dup(share.fd);check(copy>=0,"duplicate external FD");
	fd=share.fd;close(share.fd);
	close(second);
	check(count()==initial+1,"second close preserves first file mapping");
	check(!ioctl(dip,DET_BUF,&fd) && count()==initial,"detach after original FD closed");
	check(ioctl(dip,DET_BUF,&fd)<0 && errno==ENOENT,"double detach rejected");
	b.fd=copy;b.check=0;
	check(!ioctl(dip,SET_BUF,&b) && count()==initial+1,"map duplicate");
	b.check=1;b.offset=8192;
	check(ioctl(dip,SET_BUF,&b)<0 && errno==EINVAL,"offset bounds");
	check(ioctl(dip,SET_BUF,NULL)<0 && errno==EFAULT,"bad pointer");
	close(copy);munmap(va,8192);close(ion);close(dip);
	check(count()==initial,"last close releases mapping");
	puts("PASS DIP: real clock/open, MM heap map/query/detach, memory UAPI, FD lifetime, invalid requests");
	return 0;
}
