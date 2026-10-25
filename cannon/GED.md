# Cannon gralloc_extra compatibility

MIUI 14.0.6's gralloc uses GED bridge 100 with 18 metadata regions (76 input
bytes). The 6.12 baseline hardcoded 22 regions (92 bytes) both when copying
the allocation request and inside `ged_ge_alloc`. After Mali context creation
was fixed, this rejected real gralloc allocations with `Failed to region_num,
it must be 22`; RenderEngine then failed to allocate its framebuffer.

The bridge now accepts only complete 18- or 22-region layouts, checks the
copied count against the byte length, and allocates the actual number of
regions. Per-region sizes remain nonzero and at most 8192 bytes. GET/SET,
zero-on-first-read, sharing and release use the original real anon-inode
metadata storage. GE_ALLOC output must be the exact 8-byte ABI structure.
No allocator result is synthesized. Allocation errors are mapped correctly,
including valid fd 0, and an output copy fault closes the undelivered fd.

Build the separate diagnostic from the module repository root:

```sh
/usr/lib/llvm-13/bin/clang --target=aarch64-linux-gnu --gcc-toolchain=/usr \
  -fuse-ld=lld -static -O2 -Wall -Wextra -Werror \
  cannon/ged_probe.c -o out-cannon/cannon-ged-probe
```

Run `cannon-ged-probe` as root on the test kernel. It checks both layouts,
actual metadata read/write through a duplicated fd after closing the original,
CLOEXEC, fd 0, rejected sizes/counts/regions, output-pointer faults and fd
accounting. It submits no GPU jobs and accesses no physical registers.
Host compilation alone does not prove that the device ABI passed.
