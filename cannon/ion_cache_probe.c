// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <linux/cannon_ion.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
static void check(int ok, const char *step)
{
	if (!ok) { fprintf(stderr,"FAIL %s: errno=%d\n",step,errno); exit(1); }
}
static int custom(int ion, unsigned char *p, unsigned cmd)
{
	struct cannon_ion_custom c={.cmd=cmd,.arg=(uintptr_t)p};
	return ioctl(ion,CION_CUSTOM,&c);
}
static void put32(void *p, uint32_t v) { memcpy(p,&v,4); }
static void put64(void *p, uint64_t v) { memcpy(p,&v,8); }
int main(void)
{
	int ion=open("/dev/ion",O_RDWR|O_CLOEXEC);
	check(ion>=0,"open");
	for (unsigned heap=0; heap<=12; heap+=2) {
		if (heap && heap!=10 && heap!=12) continue;
		struct cannon_ion_allocation a={.len=8192,.align=4096,
			.heap_id_mask=1U<<heap,.flags=1};
		struct cannon_ion_fd shared={0};
		struct cannon_ion_handle free_handle;
		_Alignas(8) unsigned char p[128]={0};
		unsigned char *va;
		uint64_t iova=0;
		check(!ioctl(ion,CION_ALLOC,&a),"allocate cached");
		shared.handle=a.handle;
		check(!ioctl(ion,CION_SHARE,&shared),"share");
		va=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_SHARED,shared.fd,0);
		check(va!=MAP_FAILED,"mmap");
		memset(va,0x6a,8192);
		for (unsigned type=0; type<3; type++) {
			memset(p,0,sizeof(p)); put32(p+8,a.handle);
			put64(p+16,(uintptr_t)va); put32(p+24,8192); put32(p+28,type);
			check(!custom(ion,p,0),"whole cache sync");
			check(va[0]==0x6a && va[8191]==0x6a,"cache sync preserved data");
		}
		put32(p+24,0); put64(p+16,0);
		check(!custom(ion,p,0),"implicit whole-buffer sync");
		put32(p+24,8193);
		check(custom(ion,p,0)<0 && errno==EINVAL,"oversized range rejected");
		put32(p+24,8192); put64(p+16,(uintptr_t)va+8192);
		check(custom(ion,p,0)<0 && errno==EINVAL,"foreign VA rejected");
		put64(p+16,(uintptr_t)va); put32(p+28,6);
		check(custom(ion,p,0)<0 && errno==EOPNOTSUPP,"unsupported global flush rejected");
		if (heap) {
			put32(p+28,2); put64(p+16,(uintptr_t)va+256); put32(p+24,512);
			check(!custom(ion,p,0),"real partial VA sync");
			memset(p,0,sizeof(p)); put32(p,8); put32(p+8,a.handle); put32(p+16,9<<5);
			check(!custom(ion,p,1),"real DIP mapping");
			memcpy(&iova,p+40,8);
			for (unsigned type=3; type<6; type++) {
				memset(p,0,sizeof(p)); put32(p+8,a.handle);
				put32(p+24,512); put32(p+28,type); put64(p+32,iova+256);
				check(!custom(ion,p,0),"real partial IOVA sync");
			}
			put64(p+32,iova+8192);
			check(custom(ion,p,0)<0 && errno==EINVAL,"IOVA outside buffer rejected");
		}
		memset(p,0,sizeof(p)); put32(p+8,0);
		check(custom(ion,p,0)<0 && errno==EINVAL,"invalid handle rejected");
		munmap(va,8192); close(shared.fd);
		free_handle.handle=a.handle;
		check(!ioctl(ion,CION_FREE,&free_handle),"free");
		printf("PASS cache: heap=%u whole/VA/IOVA/native DMA-BUF + invalid requests\n",heap);
	}
	close(ion);
	return 0;
}
