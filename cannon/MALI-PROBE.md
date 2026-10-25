# Cannon r32p1 JM ABI probe

`mali_probe.c` uses the checked-in Mali r32p1 JM UAPI headers, not hand-written ioctl
numbers or a vendor EGL library. It prints each stage before entering the driver:

1. Open `/dev/mali0` with `O_RDWR | O_CLOEXEC`.
2. Negotiate JM API 11.38 using `VERSION_CHECK`.
3. Call `SET_FLAGS` with flags 0 to create a normal userspace context.
4. Query `GET_GPUPROPS` size, read the data, validate every packed little-endian
   property record, and print the GPU ID and revisions if present.
5. With `--alloc`, create the non-accessible tracking mapping and allocate/free one
   committed 4 KiB page. The returned handle is left unmapped, so a SAME_VA cookie
   can be freed through `MEM_FREE`. This deliberately does not confuse a mapped
   SAME_VA allocation, which must instead be released by `munmap`, with a cookie.

The program never calls `JOB_SUBMIT`, maps GPU registers, or executes GPU code.
Opening the device can cause the kernel's required dummy-job workaround to load;
that remains part of the unmodified driver. The optional allocation does not test
GPU execution, fences, cache coherency, or display output.

## Build on the host

Run from the module repository root:

```sh
MALI_UAPI="$PWD/kernel_device_modules-6.12/drivers/gpu/mediatek/mali-valhall/gpu/arm/midgard/include/uapi"
/usr/lib/llvm-13/bin/clang --target=aarch64-linux-gnu --gcc-toolchain=/usr \
    -fuse-ld=lld -static -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$MALI_UAPI" cannon/mali_probe.c -o out-cannon/cannon-mali-probe
file out-cannon/cannon-mali-probe
```

This creates an independent, statically linked AArch64 executable. It does not run
`make`, change the kernel configuration, or write any module/build artifact other
than `out-cannon/cannon-mali-probe`.

## Run and interpret

On the authorized test device, after copying the binary to `/data/local/tmp/` and
making it executable, run as root:

```sh
/data/local/tmp/cannon-mali-probe
/data/local/tmp/cannon-mali-probe --alloc
```

An optional final argument selects a different device path. Standard output is
unbuffered so the last `TRY` line identifies an incomplete/hung stage. Exit 0 is
printed only after all requested stages and cleanup succeed; exit 1 reports a
failure with numeric errno and text; exit 2 indicates invalid arguments.

- An `open` failure occurs before the ioctl handshake. In this driver,
  `mali_kbase_core_linux.c:kbase_open()` calls
  `kbase_device_firmware_init_once()`, whose JM implementation loads
  `valhall-1691526.wa`. A missing blob returns `ENODEV`; confirm the corresponding
  `WA blob missing`/firmware message in dmesg, since errno alone cannot uniquely
  identify its cause. `ENOENT` usually means the device node itself is absent;
  `EACCES`/`EPERM` can indicate permissions or SELinux.
- `VERSION_CHECK` failure or an incompatible returned major identifies the
  handshake/UAPI stage. An ioctl success with a changed version is reported, not
  silently treated as an exact r32p1 match.
- `SET_FLAGS/context creation` failure identifies context initialization after a
  successful handshake. With JM API 11.38 this call creates the context; merely
  opening the file does not establish that it succeeded.
- `GET_GPUPROPS` success verifies the actual returned kernel property buffer.
  `--alloc` separately checks tracking and one-page allocation/free.

For source references, see `include/uapi/gpu/arm/midgard/mali_kbase_ioctl.h`,
`jm/mali_kbase_jm_ioctl.h` (API 11.38), and `jm/mali_base_jm_kernel.h` (memory
flags/tracking handle) beneath the Mali directory. The unmapped-cookie release
path is in `mali_kbase_mem.c:kbase_mem_free()`.
