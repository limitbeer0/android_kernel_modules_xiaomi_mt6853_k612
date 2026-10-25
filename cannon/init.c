/* SPDX-License-Identifier: GPL-2.0-only */
/* ARM64 ramdisk PID 1: no libc, shell, mounted vendor, or Android service needed.
 * modules.load is resolved on the build host from depmod, not guessed here.
 * Selected with rdinit=/cannon-init. Leave /init at its original path so its
 * re-exec paths and ramdisk integrations retain their original semantics.
 */
typedef unsigned long size_t;
static long call3(long nr, long a, long b, long c)
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
	return x0;
}
static long call4(long nr, long a, long b, long c, long d)
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
	return x0;
}
static long call5(long nr, long a, long b, long c, long d, long e)
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory");
	return x0;
}
static int logfd = -1;
static char logbuf[768];
static size_t loglen;
static void logstr(const char *s)
{
	while (*s) {
		char c = *s++;
		logbuf[loglen++] = c;
		if (c == '\n' || loglen == sizeof(logbuf)) {
			/* Each /dev/kmsg write is a record: emit complete lines. */
			call3(64, logfd, (long)logbuf, loglen);
			loglen = 0;
		}
	}
}
__attribute__((noreturn)) static void fail(const char *s, long code)
{
	char digits[24]; int n = 0;
	logstr("<0>cannon-init: "); logstr(s); logstr(" errno=");
	if (code < 0) code = -code;
	do { digits[n++] = '0' + code % 10; code /= 10; } while (code);
	while (n) { char digit[2] = {digits[--n], 0}; logstr(digit); }
	logstr("; stopping before Android init\n");
	/* Do not continue with a partially loaded dependency set. Keep PID 1 alive
	 * so the console/pstore retain the failure; the hardware WDT can recover.
	 */
	for (;;) { long ts[2] = {60, 0}; call3(101, (long)ts, 0, 0); }
}
static int watchdog_fd = -1;
static int watchdog_probe_only = 1;
static int watchdog_crypto_test;
static int watchdog_crypto_write;
static int watchdog_android_long;

static void watchdog_arm(void)
{
	long marker, result;
	int pretimeout = 0, timeout = 20;
	char mode = 0;

	marker = call4(56, -100, (long)"/cannon-watchdog-test", 02000000, 0);
	if (marker == -2) return;
	if (marker < 0) fail("open watchdog test marker", marker);
	result = call3(63, marker, (long)&mode, 1);
	if (result < 0) fail("read watchdog test mode", result);
	watchdog_crypto_write = mode == 'G';
	watchdog_crypto_test = mode == 'F' || watchdog_crypto_write;
	watchdog_android_long = mode == 'L';
	watchdog_probe_only = mode != 'A' && !watchdog_android_long && !watchdog_crypto_test;
	/* Android mode needs time for post-fs-data and the Keymaster service. */
	if (mode == 'A' || watchdog_android_long)
		timeout = 30;
	call3(57, marker, 0, 0);
	call4(33, -100, (long)"/dev/watchdog", 0020000 | 0600, (10 << 8) | 130);
	watchdog_fd = call4(56, -100, (long)"/dev/watchdog", 1 | 02000000, 0);
	if (watchdog_fd < 0) fail("open watchdog", watchdog_fd);
	/* Disable the IRQ pretimeout: even a wedged IRQ path must cause reset. */
	result = call3(29, watchdog_fd, 0xc0045708, (long)&pretimeout);
	if (result < 0 && result != -95) fail("disable watchdog pretimeout", result);
	result = call3(29, watchdog_fd, 0xc0045706, (long)&timeout);
	if (result < 0) fail("set watchdog timeout", result);
	if (timeout != (mode == 'A' || watchdog_android_long ? 30 : 20))
		fail("unexpected watchdog timeout", 22);
	logstr(watchdog_crypto_write ?
		"<5>cannon-init: synthetic crypto write test watchdog armed for 20 seconds\n" :
		watchdog_crypto_test ?
		"<5>cannon-init: read-only crypto diagnostic watchdog armed for 20 seconds\n" :
		watchdog_probe_only ?
		"<5>cannon-init: probe watchdog armed for 20 seconds; no IRQ pretimeout\n" :
		"<5>cannon-init: android watchdog armed for 30 seconds; no IRQ pretimeout\n");
	/*
	 * Probe-only mode stops before Android exec. Mode A continues to Android
	 * while keeping the WDT active across CLOEXEC for mount-stage logging.
	 */
}

static void watchdog_ping(void)
{
	long result;

	if (watchdog_fd < 0) return;
	result = call3(64, watchdog_fd, (long)".", 1);
	if (result != 1) fail("watchdog keepalive", result);
}

static void android_watchdog_budget(void)
{
	long pid, result;

	if (!watchdog_android_long) return;
	pid = call5(220, 17, 0, 0, 0, 0); /* fork, SIGCHLD */
	if (pid < 0) fail("fork bounded watchdog helper", pid);
	if (pid) {
		logstr("<5>cannon-init: Android diagnostic budget 90 seconds; three bounded keepalives\n");
		return;
	}
	for (int i = 0; i < 3; i++) {
		long ts[2] = {20, 0};
		do {
			result = call3(101, (long)ts, (long)ts, 0);
		} while (result == -4);
		if (result < 0) fail("watchdog helper sleep", result);
		watchdog_ping();
		logstr("<5>cannon-init: bounded watchdog keepalive\n");
	}
	/* Keep the descriptor open, stop pinging. Hardware resets 30s later.
	 * A failed helper or denied write still leaves the 30s hardware fallback.
	 */
	for (;;) { long ts[2] = {60, 0}; call3(101, (long)ts, 0, 0); }
}

static void driver_snapshot(void)
{
	static char snapshot[32768];
	static const char begin[] = "\nCANNON_DRIVER_SNAPSHOT_BEGIN\n";
	static const char end[] = "\nCANNON_DRIVER_SNAPSHOT_END\n";
	long fd, size, written;

	if (watchdog_fd < 0) return;
	/* syslog READ_ALL is non-destructive. Keep the last 32KiB of pre-Android
	 * driver messages in the independent pmsg ring, before AVC floods wrap
	 * the console ring. /mnt is the private devtmpfs from arm_recovery_boot.
	 */
	size = call3(116, 3, (long)snapshot, sizeof(snapshot));
	if (size <= 0) return;
	fd = call4(56, -100, (long)"/mnt/pmsg0", 1 | 02000000, 0);
	if (fd < 0) return;
	call3(64, fd, (long)begin, sizeof(begin) - 1);
	written = call3(64, fd, (long)snapshot, size);
	call3(64, fd, (long)end, sizeof(end) - 1);
	call3(57, fd, 0, 0);
	logstr(written == size ? "<5>cannon-init: driver snapshot saved in pmsg\n" :
		"<4>cannon-init: short pmsg driver snapshot write\n");
}

static char list[32768];
static char path[512];

/*
 * Android diagnostic mode only: mark misc with boot-recovery so the LK enters
 * Recovery automatically when the watchdog resets, without a volume-key press.
 */
static void arm_recovery_boot(void)
{
	static int armed;
	char msg[32], verify[32];
	long dev = -1, result, i;
	int j;
	static const char bootrec[] = "boot-recovery";

	if (watchdog_probe_only || armed)
		return;
	for (j = 0; j < (int)sizeof(msg); j++)
		msg[j] = 0;
	for (j = 0; j < (int)sizeof(bootrec) - 1; j++)
		msg[j] = bootrec[j];
	/*
	 * The stock ramdisk /dev is a static directory; devtmpfs is mounted by
	 * Android init later.  Mount a private devtmpfs on /mnt so the UFS misc
	 * block device becomes visible before we hand over to /init.
	 */
	call3(34, -100, (long)"/mnt", 0755);
	call5(40, (long)"devtmpfs", (long)"/mnt", (long)"devtmpfs", 0, 0);
	/* Wait up to ~3s for the UFS misc block device to show up. */
	for (i = 0; i < 30; i++) {
		dev = call4(56, -100, (long)"/mnt/sdc2", 2, 0); /* O_RDWR */
		if (dev >= 0)
			break;
		{ long ts[2] = {0, 100000000}; call3(101, (long)ts, 0, 0); }
	}
	if (dev < 0) {
		logstr("<5>cannon-init: misc device not ready; skipping recovery boot\n");
		return;
	}
	result = call3(64, dev, (long)msg, sizeof(msg));
	result = result == sizeof(msg) ? call3(82, dev, 0, 0) : -5; /* fsync */
	if (!result) result = call3(62, dev, 0, 0); /* lseek, SEEK_SET */
	if (!result) result = call3(63, dev, (long)verify, sizeof(verify));
	if (result == sizeof(verify)) {
		for (j = 0; j < (int)sizeof(msg); j++)
			if (msg[j] != verify[j]) break;
		armed = j == (int)sizeof(msg);
	}
	call3(57, dev, 0, 0);
	if (armed)
		logstr("<5>cannon-init: boot-recovery synced and verified; panic/watchdog reset enters Recovery\n");
	else
		fail("boot-recovery write/fsync/readback", result < 0 ? result : 5);
}

static int is_ufs_module(const char *name)
{
	static const char ufs[] = "/lib/modules/ufs-mt6853.ko";
	unsigned int i;

	for (i = 0; i < sizeof(ufs); i++)
		if (name[i] != ufs[i]) return 0;
	return 1;
}

/*
 * isee.ko creates the TEE devices before Android ueventd runs, so their
 * default 0600 root:root mode blocks teei_daemon (system).  devtmpfs is a
 * single superblock, so fixing modes through our private mount applies to
 * /dev once Android mounts it.
 */
static void fix_tee_dev_modes(void)
{
	static const char * const names[] = {
		"/mnt/isee_tee0", "/mnt/teei_client", "/mnt/teei_config",
		"/mnt/tz_vfs", "/mnt/teei_fp", "/mnt/ut_keymaster",
		"/mnt/rpmb0", "/mnt/isee_tee1", "/mnt/isee_tee2",
	};
	char path[64];
	int i, j;

	if (watchdog_probe_only)
		return;
	for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
		const char *n = names[i];
		for (j = 0; n[j] && j < (int)sizeof(path) - 1; j++)
			path[j] = n[j];
		path[j] = 0;
		call4(53, -100, (long)path, 0666, 0);
	}
	logstr("<5>cannon-init: TEE device node modes relaxed\n");
}

__attribute__((noreturn)) static void crypto_diagnostic(char **envp)
{
	long pid, result;
	int status = 0;
	char *args[] = { "/cannon-fscrypt-probe", "diagnose", "/mnt/sdc59", 0 };

	if (watchdog_crypto_write)
		args[1] = "roundtrip";
	arm_recovery_boot();
	logstr("<5>cannon-init: crypto diagnostic; Android init will not run\n");
	/* clone(SIGCHLD) without CLONE_VM is fork on arm64. PID1 stays alive. */
	pid = call5(220, 17, 0, 0, 0, 0);
	if (pid < 0) fail("fork crypto diagnostic", pid);
	if (!pid) {
		/* The static reader's complete output goes to console/pstore. */
		call3(24, logfd, 1, 0); /* dup3 */
		call3(24, logfd, 2, 0);
		result = call3(221, (long)args[0], (long)args, (long)envp);
		logstr("<0>cannon-init: exec crypto reader failed\n");
		call3(94, result ? 127 : 0, 0, 0); /* exit_group, child only */
		for (;;) { }
	}
	do {
		result = call4(260, pid, (long)&status, 0, 0); /* wait4 */
	} while (result == -4);
	if (result != pid) fail("wait crypto diagnostic", result);
	logstr(status ? "<4>cannon-init: crypto diagnostic reported failure\n" :
		watchdog_crypto_write ? "<5>cannon-init: synthetic write/readback test passed\n" :
		"<5>cannon-init: crypto diagnostic passed both read paths\n");
	logstr("<5>cannon-init: waiting for watchdog Recovery\n");
	for (;;) { long ts[2] = {60, 0}; call3(101, (long)ts, 0, 0); }
}

__attribute__((noreturn,used)) void start_c(long *stack)
{
	long argc = stack[0];
	char **argv = (char **)&stack[1];
	char **envp = argv + argc + 1;
	long fd, count = 0, result;
	unsigned long pos = 0;
	/* /dev exists in the stock ramdisk; a static node also works before init
	 * mounts devtmpfs. mknodat returns EEXIST when the node is already there.
	 */
	call4(33, -100, (long)"/dev/kmsg", 0020000 | 0600, (1 << 8) | 11);
	logfd = call4(56, -100, (long)"/dev/kmsg", 1 | 02000000, 0);
	watchdog_arm();
	logstr("<6>cannon-init: loading boot ramdisk modules\n");
	fd = call4(56, -100, (long)"/lib/modules/modules.load", 02000000, 0);
	if (fd < 0) fail("open modules.load", fd);
	for (;;) {
		result = call3(63, fd, (long)(list + count), sizeof(list) - count);
		if (result == -4) continue; /* EINTR */
		if (result < 0) fail("read modules.load", result);
		if (!result) break;
		count += result;
		if ((unsigned long)count == sizeof(list)) fail("modules.load too large", 7);
	}
	call3(57, fd, 0, 0);
	while (pos < (unsigned long)count) {
		unsigned long n = 0;
		const char prefix[] = "/lib/modules/";
		for (unsigned long i = 0; i < sizeof(prefix)-1; i++) path[n++] = prefix[i];
		while (pos < (unsigned long)count && list[pos] != '\n') {
			char c = list[pos++];
			if (n >= sizeof(path)-1 || !((c >= 'a' && c <= 'z') ||
			    (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			    c == '_' || c == '-' || c == '.')) fail("bad modules.load", 22);
			path[n++] = c;
		}
		if (pos < (unsigned long)count) pos++;
		if (n == sizeof(prefix)-1) continue;
		path[n] = 0;
		logstr("<6>cannon-init: "); logstr(path); logstr("\n");
		fd = call4(56, -100, (long)path, 02000000, 0);
		if (fd < 0) fail(path, fd);
		result = call3(273, fd, (long)"", 0); /* finit_module; no force flags */
		call3(57, fd, 0, 0);
		if (result < 0 && result != -17) fail(path, result); /* EEXIST */
		watchdog_ping();
		/* Arm before TEE/graphics probes can panic, once UFS can access misc. */
		if (is_ufs_module(path)) arm_recovery_boot();
	}
	if (watchdog_crypto_test)
		crypto_diagnostic(envp);
	if (watchdog_fd >= 0 && watchdog_probe_only) {
		logstr("<5>cannon-init: diagnostic modules loaded; waiting for SCSI scan; Android filesystems stay unmounted\n");
		for (;;) { long ts[2] = {60, 0}; call3(101, (long)ts, 0, 0); }
	}
	logstr("<6>cannon-init: module load complete; exec original /init\n");
	arm_recovery_boot();
	fix_tee_dev_modes();
	driver_snapshot();
	android_watchdog_budget();
	argv[0] = "/init";
	result = call3(221, (long)"/init", (long)argv, (long)envp);
	fail("exec original init", result);
}
__asm__(".global _start\n_start:\nmov x0, sp\nbl start_c\nb .\n");
