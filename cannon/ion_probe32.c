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
void _start(void)
{
	int ion=open_ion(), other=open_ion();
	u32 masks[]={1,1U<<10,1U<<12};
	struct query q={0,0,0,0,0};
	if (ion<0 || other<0) fail("open");
	long queried=ioctl(ion, 0xc0184908, &q);
	if (queried == -25)
		say("4.14 compat quirk: heap query ENOTTY\n");
	else if (queried<0 || q.count<3)
		fail("query");
	for (u32 heap=0; heap<3; heap++) {
		for (u32 cached=0; cached<2; cached++) {
			struct allocation a={8192,4096,masks[heap],cached,0};
			struct fd_data share, imported;
			int handle;
			long address;
			volatile unsigned char *p;
			if (ioctl(ion, 0xc0144900, &a)<0) fail("alloc");
			share.handle=a.handle; share.fd=-1;
			if (ioctl(ion, 0xc0084904, &share)<0) fail("share");
			address=call(192,0,8192,3,1,share.fd,0);
			if ((u32)address>=0xfffff001U) fail("mmap2");
			p=(volatile unsigned char *)address;
			for (u32 i=0; i<8192; i++) if(p[i]) fail("zero");
			p[0]=0x6a; p[8191]=0xc5;
			imported.handle=0; imported.fd=share.fd;
			if (ioctl(other, 0xc0084905, &imported)<0) fail("import");
			handle=a.handle;
			if (ioctl(ion,0xc0044901,&handle)<0) fail("free original");
			handle=imported.handle;
			if (ioctl(other,0xc0044901,&handle)<0) fail("free import");
			if (ioctl(other,0xc0044901,&handle)!=-22) fail("stale handle");
			call(6,share.fd,0,0,0,0,0);
			if(p[0]!=0x6a || p[8191]!=0xc5) fail("mapping lifetime");
			call(91,address,8192,0,0,0,0);
		}
	}
	call(6,ion,0,0,0,0,0); call(6,other,0,0,0,0,0);
	say("PASS ARM32: 3 heaps x cached/uncached, alloc/share/import/mmap2/free/lifetime\n");
	call(1,0,0,0,0,0,0);
	__builtin_unreachable();
}
