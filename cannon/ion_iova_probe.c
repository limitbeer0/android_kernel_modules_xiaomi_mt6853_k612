// SPDX-License-Identifier: GPL-2.0-only
/* Real CAM-domain mappings only; does not submit DMA to camera hardware. */
#define _GNU_SOURCE
#include <linux/cannon_ion.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void check(int ok, const char *step)
{
	if (!ok) {
		fprintf(stderr, "FAIL %s: errno=%d (%s)\n",step,errno,strerror(errno));
		exit(1);
	}
}
static void put32(unsigned char *p,uint32_t v) { memcpy(p,&v,4); }
static int custom(int ion,unsigned char *p)
{
	struct cannon_ion_custom c={.cmd=1,.arg=(uintptr_t)p};
	return ioctl(ion,CION_CUSTOM,&c);
}
static uint64_t get_iova(int ion,int handle,unsigned int port,size_t size)
{
	_Alignas(8) unsigned char p[1024]={0};
	uint64_t address=0,length=0;
	put32(p,8); put32(p+8,handle); put32(p+16,port);
	check(custom(ion,p)==0,"GET_IOVA");
	memcpy(&address,p+40,8); memcpy(&length,p+48,8);
	check(address>=0x200000000ULL && address<0x300000000ULL,"actual CAM domain");
	check(length==size,"whole DMA buffer length");
	return address;
}
int main(void)
{
	int ion=open("/dev/ion",O_RDWR|O_CLOEXEC);
	check(ion>=0,"open");
	for (unsigned int heap=10;heap<=12;heap+=2)
		for (unsigned int cached=0;cached<2;cached++) {
			size_t size=cached?((15024000+4095)&~4095):8192;
			struct cannon_ion_allocation a={.len=size,.align=4096,
				.heap_id_mask=1U<<heap,.flags=cached};
			struct cannon_ion_fd shared, imported;
			struct cannon_ion_handle freed;
			_Alignas(8) unsigned char p[1024]={0};
			uint64_t address;
			int other;
			check(ioctl(ion,CION_ALLOC,&a)==0,"allocate");
			put32(p,1); put32(p+8,a.handle);
			strcpy((char *)p+16,"cannon-ion-iova-probe");
			check(custom(ion,p)==0,"set unique diagnostic name");
			address=get_iova(ion,a.handle,16<<5,size);
			check(get_iova(ion,a.handle,(13<<5)|6,size)==address,"stable across CAMSV/RAW");
			check(get_iova(ion,a.handle,17<<5,size)==address,"shared RAWA/RAWB domain");
			shared=(struct cannon_ion_fd){.handle=a.handle,.fd=-1};
			check(ioctl(ion,CION_SHARE,&shared)==0,"share");
			freed.handle=a.handle;
			check(ioctl(ion,CION_FREE,&freed)==0,"free all ION handles");
			usleep(600000);
			other=open("/dev/ion",O_RDWR|O_CLOEXEC);
			check(other>=0,"second client");
			imported=(struct cannon_ion_fd){.fd=shared.fd};
			check(ioctl(other,CION_IMPORT,&imported)==0,"import fd-only mapped buffer");
			check(get_iova(other,imported.handle,16<<5,size)==address,"fd-only IOVA lifetime");
			memset(p,0,sizeof(p));
			put32(p,8); put32(p+8,imported.handle); put32(p+16,16<<5);
			put32(p+20,1);
			errno=0;
			check(custom(other,p)==-1 && errno==EOPNOTSUPP,"reject secure request");
			put32(p+20,0); put32(p+28,0x100000);
			errno=0;
			check(custom(other,p)==-1 && errno==EOPNOTSUPP,"reject unsupported fixed range");
			put32(p+28,0); put32(p+16,0xffffffff);
			errno=0;
			check(custom(other,p)==-1 && errno==EOPNOTSUPP,"reject invalid port");
			put32(p+16,16<<5); put32(p+8,0);
			errno=0;
			check(custom(other,p)==-1 && errno==EINVAL,"reject stale/invalid handle");
			freed.handle=imported.handle;
			check(ioctl(other,CION_FREE,&freed)==0,"free final import");
			close(shared.fd); close(other);
			printf("PASS CAM IOVA: heap=%u cached=%u size=%zu address=%#llx\n",
			       heap,cached,size,(unsigned long long)address);
		}
	close(ion);
	usleep(600000);
	puts("PASS CAM DMA: domain/length/reuse/import/fd-only lifetime and invalid requests");
	return 0;
}
