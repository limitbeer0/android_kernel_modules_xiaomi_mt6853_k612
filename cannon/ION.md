# Cannon 4.14 ION ABI over 6.12 DMA heaps

The official MIUI14 allocator still issues the handle-based `ALLOC`, `SHARE`,
and `FREE` ioctls. A stock screenshot traced with strace confirmed this sequence.
`cannon-ion.ko` implements those handles using the common kernel's real `system`
and `system-uncached` DMA heaps. It also supports MAP, IMPORT, SYNC and heap query.
The allocation backing, DMA-BUF attachment, mmap and cache maintenance stay in
the 6.12 heap driver. This does not port the old 4.14 ION allocator.

Only ordinary page heaps 0, 10 and 12 are advertised. Secure/protected heaps,
carveout/framebuffer heaps and MTK custom IOVA/configuration ioctls are not
implemented; unsupported requests return errors. In particular this is not yet
the complete MTK multimedia memory ABI or a replacement for the IOMMU driver.

Build the 64-bit and actual ARM EABI test executables with Clang13:

```sh
bash cannon/build_ion_probes.sh
```

Test51 was flashed and verified on 6.12.23-cannon-bringup+, with 38 ramdisk ko,
adb uid0 and global SELinux Enforcing. Both ABIs passed allocation zero-fill,
cached/uncached mappings, cross-client sharing/import, repeated-import reference
counts, stale-handle rejection, shared bytes and mapping lifetime after fd close.
All six 64-bit cases also verified that a read-only ioctl output causes EFAULT
without leaking a descriptor. DMA-BUF debug statistics returned to zero objects
after the probes exited. The official allocator remained pid374/running at
uptime514.10s. HWC still failed without DRM, and Mali was not present.

The same positive tests passed on the working 4.14 kernel. Two source-confirmed
4.14 quirks are reported separately: heap-list query fills the buffer but returns
EINVAL, and compat heap query returns ENOTTY. Only the stock test permits those
quirks. Stock also leaks one fd on SHARE output EFAULT; the new layer does not.

Image SHA256: `95e8eb3282f2c6947fd06e6388c3e04aa252a3727b96253523855ad0a80398f9`.
Evidence: `../../device-cannon/test51/`, including the probes, stock ioctl trace,
runtime states, live logcat/dmesg, pstore, complete expdb readback and result.json.
The direct expdb channel retained 3,557,337 bytes with a valid header CRC, up to
uptime604.94s. WDT returned to Recovery, then the working MIUI14 full boot was
restored and normal 4.14/boot_completed1/CEtrue/Enforcing were verified.
