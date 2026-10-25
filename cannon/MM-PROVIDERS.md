# Cannon 6.12 multimedia providers

Test55 flashed and verified the existing 6.12 baseline MT6853 SCPSYS, SMI and IOMMU drivers, built using Clang/LLD13 and loaded as ramdisk ko. There are now46 staged modules. This is provider/mapping validation, not a working DRM scanout or GPU.

## Power and ordering

`cannon-mm-providers.dtsi` is now included by the default `cannon.dts`. The power-controller's old clock binding put infracfg at resource0. The 6.12 SCPSYS binding uses SPM10006000, power-domain cells, preserved numeric phandle49, and36 clocks translated into Cannon's legacy clock-ID namespace. The MT6853 probe rejects a legacy resource list before MMIO access; packaging validates the post-stock-DTBO SPM and display-domain bindings.

The explicit Cannon preserve-boot-power-state property adopts the19 domains' observed status without resetting SRAM/bus protection or powering every unused domain on. Test55 found md, disp and adsp_dormant on; adopted-on domains are held ALWAYS_ON because their firmware clock/regulator references were not acquired by the normal power-on path. This does not claim complete dynamic power-gating validation. Other domains start off and retain the real baseline power-on implementation.

The helper module is `cannon-scpsys-core.ko`, distinct from the common kernel's legacy module basename. It supplies the baseline helpers without registering another legacy mtk-scpsys platform driver; `mtk-scpsys-mt6853.ko` owns Cannon's provider. The staging tool places both after UFS, so PID1 first synchronizes and reads back the Recovery BCB. An incompatible symbol dependency that would load SCPSYS before this barrier is rejected. Eight staging tests passed.

## Real-device evidence

- SCPSYS registered19 domains. SMI common, larb0/1 and m4u14016000 have actual driver links.
- larb0 has the baseline init-power-on hold. Display stays on, common/larb0 active; larb1 and IOMMU can suspend at runtime.
- larb1 completed10 on/auto runtime-PM cycles and returned to auto/suspended.
- The optional `cannon-iommu-probe.ko` checks three coherent-allocation pages and two streaming-map pages against `iommu_iova_to_phys`; all5 passed. Ten unbind/rebind cycles also passed. Allocations and mappings are released after each test.
- The diagnostic DTB uses the unused DISP_FAKE0 port (larb0 port3). No display engine DMA job is submitted, so this does not prove an end-to-end display DMA transfer. The normal DTB has no diagnostic client.
- GCE's four-word scratch test remained PASS. Normal6.12 adb was uid0, global SELinux Enforcing; explicit checks continued to459.95s.
- Finite WDT returned to Fox automatically. Direct expdb retained3,676,556 bytes through605.712963s with a valid CRC,1022 commits and2 dropped records. Native512KiB head/tail capture also retained the latest boot.

Evidence is in `../../device-cannon/test55/`; image SHA256 `6a88be085d21d27c6a9ae24fa23cd7be5a312c978dde1a3e4c2223306aea6d13`. Rebuilding the diagnostic wrapper after enabling providers in the default DTS produced a byte-identical DTB. The old test54 SPM binding is now rejected by packaging.

LK, Preloader and the independent stock DTBO were not modified. The working MIUI14 full boot was restored and its hash verified; see result.json for the final normal-runtime verification.
