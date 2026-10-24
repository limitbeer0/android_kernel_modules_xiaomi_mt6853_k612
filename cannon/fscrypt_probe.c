// SPDX-License-Identifier: GPL-2.0-only
/* Controlled 4.14/6.12 fscrypt interoperability fixture. No real user keys or
 * files are read. "prepare" creates a new empty test directory using a public
 * synthetic v1 key; "verify" installs that same key in memory and only reads.
 * Use verify on a read-only userdata mount for the first 6.12 comparison.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fscrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_DIR "cannon-crypto-probe-v1"
#define INLINE_DIR TEST_DIR "-612-inline"
#define SOFTWARE_DIR TEST_DIR "-612-software"
#define TEST_FILE "known-plaintext.bin"
#define TEST_BYTES 8192

static const unsigned char descriptor[8] = {
	0xca, 0x66, 0x6e, 0x6f, 0x6e, 0x00, 0x00, 0x01
};

static void die(const char *what)
{
	perror(what);
	exit(1);
}

static void install_test_key(int root)
{
	struct fscrypt_add_key_arg *arg = calloc(1, sizeof(*arg) + 64);
	int i, ret, saved_errno;

	if (!arg)
		die("allocate test key argument");
	arg->key_spec.type = FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR;
	memcpy(arg->key_spec.u.descriptor, descriptor, sizeof(descriptor));
	arg->raw_size = 64;
	for (i = 0; i < 64; i++)
		arg->raw[i] = i ^ 0x5a; /* public fixture key, never a user key */
	ret = ioctl(root, FS_IOC_ADD_ENCRYPTION_KEY, arg);
	saved_errno = errno;
	free(arg);
	errno = saved_errno;
	if (ret)
		die("add synthetic fscrypt key");
}

static void verify_policy(int dir)
{
	struct fscrypt_get_policy_ex_arg arg = { .policy_size = sizeof(arg.policy) };
	struct fscrypt_policy_v1 *p = &arg.policy.v1;

	if (ioctl(dir, FS_IOC_GET_ENCRYPTION_POLICY_EX, &arg))
		die("get fixture encryption policy");
	if (p->version != FSCRYPT_POLICY_V1 ||
	    p->contents_encryption_mode != FSCRYPT_MODE_AES_256_XTS ||
	    p->filenames_encryption_mode != FSCRYPT_MODE_AES_256_CTS || p->flags ||
	    memcmp(p->master_key_descriptor, descriptor, sizeof(descriptor))) {
		fprintf(stderr, "fixture has an unexpected encryption policy\n");
		exit(1);
	}
}

static void fill_pattern(unsigned char *data)
{
	size_t i;

	for (i = 0; i < TEST_BYTES; i++)
		data[i] = (unsigned char)(i * 73 + 19);
}

static int fixture(const char *mode, const char *mountpoint, const char *name)
{
	unsigned char expected[TEST_BYTES], actual[TEST_BYTES];
	struct stat st;
	int root, parent, dir, fd, prepare;
	size_t done = 0;
	ssize_t n;

	prepare = !strcmp(mode, "prepare");
	root = open(mountpoint, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (root < 0)
		die("open userdata mount");
	parent = openat(root, "unencrypted", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (parent < 0)
		die("open existing unencrypted parent");
	/* Never create a test hierarchy underneath an encrypted parent. */
	{
		struct fscrypt_get_policy_ex_arg arg = { .policy_size = sizeof(arg.policy) };
		if (!ioctl(parent, FS_IOC_GET_ENCRYPTION_POLICY_EX, &arg) || errno != ENODATA) {
			fprintf(stderr, "expected an existing unencrypted parent\n");
			return 1;
		}
	}
	install_test_key(root);
	if (prepare && mkdirat(parent, name, 0700))
		die("create new fixture directory (existing fixtures are never overwritten)");
	dir = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (dir < 0)
		die("open fixture directory");
	if (fstat(dir, &st) || st.st_uid != getuid() || (st.st_mode & 0777) != 0700) {
		fprintf(stderr, "fixture owner/mode check failed\n");
		return 1;
	}
	if (prepare) {
		struct fscrypt_policy_v1 policy = {
			.version = FSCRYPT_POLICY_V1,
			.contents_encryption_mode = FSCRYPT_MODE_AES_256_XTS,
			.filenames_encryption_mode = FSCRYPT_MODE_AES_256_CTS,
		};
		memcpy(policy.master_key_descriptor, descriptor, sizeof(descriptor));
		if (ioctl(dir, FS_IOC_SET_ENCRYPTION_POLICY, &policy))
			die("set policy on new empty fixture directory");
	}
	verify_policy(dir);
	fill_pattern(expected);
	fd = openat(dir, TEST_FILE, O_NOFOLLOW | O_CLOEXEC |
		    (prepare ? O_WRONLY | O_CREAT | O_EXCL : O_RDONLY), 0600);
	if (fd < 0)
		die("open fixture file");
	while (done < TEST_BYTES) {
		n = prepare ? write(fd, expected + done, TEST_BYTES - done) :
			      read(fd, actual + done, TEST_BYTES - done);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			fprintf(stderr, "fixture I/O failed at byte %zu (result %zd, errno %d)\n",
				done, n, errno);
			return 1;
		}
		done += n;
	}
	if (prepare) {
		if (fsync(fd) || fsync(dir) || fsync(parent))
			die("sync fixture");
		printf("cannon-fscrypt: fixture created: %s\n", name);
	} else {
		if (fstat(fd, &st) || st.st_size != TEST_BYTES ||
		    memcmp(expected, actual, TEST_BYTES)) {
			fprintf(stderr, "cannon-fscrypt: FAIL: plaintext/size mismatch\n");
			return 1;
		}
		puts("cannon-fscrypt: PASS: all 8192 bytes and v1 policy match");
	}
	close(fd);
	close(dir);
	close(parent);
	close(root);
	return 0;
}

static int run_fixture(const char *mode, const char *target, const char *name)
{
	pid_t pid;
	int status;

	fflush(stdout);
	pid = fork();
	if (pid < 0)
		die("fork fixture worker");
	if (!pid)
		exit(fixture(mode, target, name));
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			die("wait for fixture worker");
	}
	return !WIFEXITED(status) || WEXITSTATUS(status);
}

/* Early-boot diagnostic: no Android init, no filesystem repair/replay, no
 * userdata writes. Unmount between implementations to discard inode/pagecache
 * and per-superblock keys, so the second read cannot reuse the first result.
 */
static int diagnose(const char *device)
{
	const char * const options[] = { "norecovery,inlinecrypt", "norecovery" };
	const char *target = "/cannon-data";
	struct stat st;
	int i, failed = 0;

	if (lstat(device, &st) || !S_ISBLK(st.st_mode))
		die("expected userdata block device");
	if (mkdir(target, 0700) && errno != EEXIST)
		die("create diagnostic mountpoint");
	if (lstat(target, &st) || !S_ISDIR(st.st_mode) || st.st_uid != 0)
		die("check diagnostic mountpoint");
	for (i = 0; i < 2; i++) {
		struct statvfs sv;
		int status;

		printf("cannon-fscrypt: mount read-only with %s\n", options[i]);
		fflush(stdout);
		if (mount(device, target, "f2fs", MS_RDONLY | MS_NOATIME |
			  MS_NODEV | MS_NOSUID | MS_NOEXEC, options[i]))
			die("read-only norecovery mount");
		if (statvfs(target, &sv) || !(sv.f_flag & ST_RDONLY))
			die("verify mount is read-only");
		status = run_fixture("verify", target, TEST_DIR);
		if (status)
			failed = 1;
		printf("cannon-fscrypt: %s reader status=%d\n", options[i], status);
		fflush(stdout);
		if (umount(target))
			die("unmount read-only diagnostic filesystem");
	}
	return failed;
}

/* Explicit second-stage test, after diagnose() has passed. The only new files
 * are two O_EXCL fixtures in fresh directories. Normal F2FS checkpoints are
 * needed for this write test; no filesystem tools or Android services run.
 * Verify these files again under the original 4.14 after restoring boot.
 */
static int roundtrip(const char *device)
{
	const char * const options[] = { "inlinecrypt", "" };
	const char * const names[] = { INLINE_DIR, SOFTWARE_DIR };
	const char *target = "/cannon-data";
	int i, status;

	if (diagnose(device)) {
		fprintf(stderr, "cannon-fscrypt: refusing write test: read comparison failed\n");
		return 1;
	}
	for (i = 0; i < 2; i++) {
		printf("cannon-fscrypt: create fresh fixture %s with %s\n",
		       names[i], i ? "software fscrypt" : "inlinecrypt fallback");
		fflush(stdout);
		if (mount(device, target, "f2fs", MS_NOATIME | MS_NODEV |
			  MS_NOSUID | MS_NOEXEC, options[i]))
			die("mount userdata for controlled new-fixture write");
		status = run_fixture("prepare", target, names[i]);
		if (umount(target))
			die("checkpoint and unmount fixture write");
		if (status)
			return 1;
		/* Reopen from disk using the other implementation after unmount. */
		if (mount(device, target, "f2fs", MS_RDONLY | MS_NOATIME | MS_NODEV |
			  MS_NOSUID | MS_NOEXEC, i ? "norecovery,inlinecrypt" : "norecovery"))
			die("read-only remount for fixture readback");
		status = run_fixture("verify", target, names[i]);
		if (umount(target))
			die("unmount fixture readback");
		if (status)
			return 1;
	}
	puts("cannon-fscrypt: new fixtures synced and read back; verify both on original 4.14");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "diagnose"))
		return diagnose(argv[2]);
	if (argc == 3 && !strcmp(argv[1], "roundtrip"))
		return roundtrip(argv[2]);
	if (argc == 3 && (!strcmp(argv[1], "prepare") || !strcmp(argv[1], "verify")))
		return fixture(argv[1], argv[2], TEST_DIR);
	if (argc == 3 && !strcmp(argv[1], "verify-inline612"))
		return fixture("verify", argv[2], INLINE_DIR);
	if (argc == 3 && !strcmp(argv[1], "verify-software612"))
		return fixture("verify", argv[2], SOFTWARE_DIR);
	fprintf(stderr, "usage: %s {prepare|verify} <userdata-mount>\n"
		"       %s {verify-inline612|verify-software612} <userdata-mount>\n"
		"       %s {diagnose|roundtrip} <userdata-block-device>\n",
		argv[0], argv[0], argv[0]);
	return 2;
}
