// SPDX-License-Identifier: GPL-2.0-only
/* Freestanding ARM EABI probe: exercises the actual compat syscall path. */
typedef unsigned int u32;
typedef unsigned long long u64;
struct allocation { u32 len, align, mask, flags; int handle; };
struct fd_data { int handle, fd; };
struct query { u32 count, reserved0; u64 heaps; u32 reserved1, reserved2; };
_Static_assert(sizeof(struct allocation) == 20, "4.14 compat allocation layout");
_Static_assert(sizeof(struct query) == 24, "heap query layout");

static long call(long n, long a, long b, long c, long d, long e, long f)
{
	register long r0 __asm__("r0") = a;
	register long r1 __asm__("r1") = b;
	register long r2 __asm__("r2") = c;
	register long r3 __asm__("r3") = d;
	register long r4 __asm__("r4") = e;
	register long r5 __asm__("r5") = f;
	register long r7 __asm__("r7") = n;
	__asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3),
			 "r"(r4), "r"(r5), "r"(r7) : "memory", "cc");
	return r0;
}

static void say(const char *s)
{
	u32 len=0;
	while (s[len]) len++;
	call(4, 1, (long)s, len, 0, 0, 0);
}
static void fail(const char *step)
{
	say("FAIL ARM32 "); say(step); say("\n");
	call(1, 1, 0, 0, 0, 0, 0);
	__builtin_unreachable();
}
static long ioctl(int fd, u32 cmd, void *data)
{
	return call(54, fd, cmd, (long)data, 0, 0, 0);
}
static int open_ion(void)
{
	return call(5, (long)"/dev/ion", 2, 0, 0, 0, 0);
}
struct dip_buffer { u32 fd, offset, address, check; };
struct dip_mem { u32 command, address, va, size_diff; };
void _start(void)
{
	int ion=open_ion();
	int dip=call(5,(long)"/dev/camera-dip",2,0,0,0,0);
	struct allocation a={8192,4096,1U<<10,0,0};
	struct fd_data share;
	struct dip_buffer b={0,0,0,0};
	struct dip_mem mem={1,0,0,0};
	if (ion<0 || dip<0) fail("DIP open");
	if (ioctl(ion,0xc0144900,&a)<0) fail("allocate");
	share.handle=a.handle; share.fd=-1;
	if (ioctl(ion,0xc0084904,&share)<0) fail("share");
	if (ioctl(ion,0xc0044901,&a.handle)<0) fail("free ION handle");
	long address=call(192,0,8192,3,1,share.fd,0);
	if ((u32)address>=0xfffff001U) fail("mmap2");
	b.fd=share.fd;
	if (ioctl(dip,0xc0104410,&b)<0) fail("DIP real map");
	b.check=1;
	if (ioctl(dip,0xc0104410,&b)<0) fail("DIP query");
	mem.address=b.address;mem.va=(u32)address;
	if (ioctl(dip,0xc010440e,&mem)<0) fail("compat tpipe mem info");
	mem.command=2;
	if (ioctl(dip,0xc010440e,&mem)<0) fail("compat command mem info");
	if (ioctl(dip,0xc010440e,(void *)0)!=-14) fail("bad compat pointer");
	mem.command=99;
	if (ioctl(dip,0xc010440e,&mem)!=-22) fail("bad compat memory command");
	call(6,share.fd,0,0,0,0,0);
	if (ioctl(dip,0xc0044411,&b.fd)<0) fail("detach after closed external FD");
	if (ioctl(dip,0xc0044411,&b.fd)!=-2) fail("double detach");
	call(91,address,8192,0,0,0,0);
	call(6,dip,0,0,0,0,0);call(6,ion,0,0,0,0,0);
	say("PASS ARM32 DIP: actual CAM DMA map/query/detach, native compat memory UAPI, closed FD lifetime, invalid inputs\n");
	call(1,0,0,0,0,0,0);
	__builtin_unreachable();
}
