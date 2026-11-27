# Cannon 4.14 ION ABI over 6.12 DMA heaps

The official MIUI14 allocator still issues the handle-based `ALLOC`, `SHARE`,
and `FREE` ioctls. A stock screenshot traced with strace confirmed this sequence.
`cannon-ion.ko` implements those handles using the common kernel's real `system`
and `system-uncached` DMA heaps. It also supports MAP, IMPORT, SYNC and heap query.
The allocation backing, DMA-BUF attachment, mmap and cache maintenance stay in
the 6.12 heap driver. This does not port the old 4.14 ION allocator.

Only ordinary page heaps 0, 10 and 12 are advertised. Secure/protected heaps,
carveout/framebuffer heaps, fixed IOVA ranges and general MTK buffer configuration
remain unsupported. Normal CAM GET_IOVA and debug metadata are implemented as
described below. Unsupported requests return errors. This is not yet
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

## Camera memory compatibility, 2026-11-27

Test118's real MIUI camera log rejected ION_SYS_SET_CLIENT_NAME and
ION_MM_SET_DEBUG_INFO; the HAL aborted image-buffer allocation. The module
now stores client names and all four buffer debug values, supports GET_DEBUG_INFO,
and applies a DMA-BUF display name. Metadata belongs to the DMA-BUF across
clients and an interval with only a shared fd, rather than a particular handle.
The native and ARM32 unions both align to eight bytes because other members
contain a u64; only the nested handle union shrinks in compat. Offsets were
cross-checked by compiling the original 4.14 struct bodies for both targets.
The initial test119 ARM32 probe used the wrong alignment and is superseded by
the corrected test120 and later results.

Normal CAM ION_MM_GET_IOVA uses actual DMA-BUF attachment and mapping through
the native MT6853 IOMMU, in the 8-12 GiB CAM domain. Separate DMA clients cover
LARB13 ports0-8 and LARB16/17 ports0-16: the native provider requires one LARB
per device. Mappings are reused only when both clients share the same IOMMU
domain. Every returned extent is checked for DMA contiguity, aperture bounds
and translation-table agreement at both endpoints. Security, coherent-mode,
fixed-range, CCU and other-port requests return errors.

An entry holds a DMA-BUF reference and any DMA attachment until no external
buffer references remain, then a 200 ms collector releases them. Active
mappings also pin this module, including an interval with only a shared fd.
Module exit synchronizes the collector. This changes no common-kernel/KMI
structure. Each DMA client exposes mapping_state for mapped buffer count,
bytes and metadata count.

Test122 booted with the same 95 ramdisk modules. Real native and ARM32 probes
passed small and 15,024,128-byte buffer mappings, CAMSV/RAWA/RAWB reuse,
cross-client import, fd-only lifetime, and invalid request handling. Mapping
counts returned to zero after closing the probe buffers. Actual MIUI camera
HAL then mapped its own buffers through port0x200; its next failure was the
missing ISP device node. Camera preview is still unavailable.

build_ion_probes.sh also builds cannon-ion-metadata-probe and
cannon-ion-iova-probe. The IOVA probe only establishes and verifies mappings;
it submits no camera DMA.

Test123 adds module pinning while a real DMA mapping exists. The full native
allocation/metadata/IOVA and ARM32 suite passed again; mappings returned to
zero, boot_completed was1, the flashed boot prefix SHA matched the packaged
image and the signed DTBO hash remained unchanged. This remains a finite
1800-second watchdog/expdb diagnostic boot, with panic recovery enabled.
