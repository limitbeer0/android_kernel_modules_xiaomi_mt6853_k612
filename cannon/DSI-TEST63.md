# Cannon test63: DSI and official HWC evidence

2026-10-31. Continue from test62's working 72-byte MTK Mali job layout.
Normal restore image remains the complete MIUI14 working boot with SHA256
8bf3c2169c5eb77974068fdd286918454f7bfba207ecd01c436bf1d94c95eabb.
Only boot was flashed. LK, Preloader and the independent stock DTBO remain
unchanged.

test63 boot SHA256:
5bb10058ed03c9aad2082c29ee807a236d5383864dd8c325b9488ada3d6f30f4.
Clang/LLD13, kernel #20, 72 ramdisk modules. Module build, strict modpost,
15 boot tests and post-stock-DTBO binding checks passed.

This diagnostic build skips unsupported Cannon self-pattern writes at
DSI +0x230/+0x234 and target-line writes at the generic +0x300 fallback.
CRTC first-config diagnostics now use the DSI driver's SoC register offsets.
No frame-completion event is synthesized and the existing startup/synchronization
sequence is preserved.

## Actual hardware observations

Probe explicitly reported LK DSI on. First-config registers showed START=1,
MODE=2, TXRX=0x3c and VACT=0x924. The read-only driver dump at about 32 seconds
was obtained with `mobile:on`, `diagnose`, then `mobile:off` through mtkfb.

| Component | Observed state |
| --- | --- |
| RDMA0 | Engine enabled, direct-link, 1080 x 2340 |
| RDMA0 scan position | Advanced and wrapped between samples about 22 ms apart |
| mutex0 | Enabled; SOF/EOF DSI0; MOD=0x147f7 |
| DSI0 | START=1, busy=1, sync-event VDO, four lanes |
| MIPI PLL | Approximately 1196 MHz |
| Mali JOB interrupt | Increased from 4045 to 4242 in a later sample |
| Mali MMU interrupt | Zero in those samples |

The first-config packet does not contain a frame-done WFE. Its `first_cfg`
callbacks depend on LK state, but this run already had a configured RDMA and
mutex. A cold-path rewrite therefore needs additional evidence.
`frame_done_idx` logs the display completion-fence index; its zero value alone
does not measure whether hardware is scanning.

At the first sample, the DRM connector reported connected/disabled/Off while
SurfaceFlinger was running. Official HWC reported missing expected CRTC and
plane properties, failed resource initialization, returned -22 from an atomic
blankDisplay commit, and later repeatedly crashed with SIGSEGV. Initial DSI
and RDMA interrupt counts were nonzero; both later stopped increasing while
Mali JOB interrupts continued. This is insufficient to distinguish a normal
blank/idle transition from a stalled engine without a simultaneous later dump.

The HWC library was read from the running official vendor partition for local
analysis. Its DrmObject::checkProperty iterates all expected names and returns
-EINVAL when any is absent. This confirms that the missing-property messages
must be addressed before judging ordinary HWC display submission.

## Next validation: test64

Cannon-only legacy plane alpha properties have real OVL consumers; details
are in HWC-ABI.md. The three unsupported CRTC features ICON_ENABLE,
ENROLL_ENABLE and SKIP_CONFIG expose only the disabled value zero. Nonzero
requests are rejected. This needs actual client validation.

The next test must check HWC resource initialization, atomic commit errors,
connector/CRTC state, actual framebuffer submission and continuing interrupts.
Keep the hardware EOF synchronization and inspect any remaining timeout after
the client successfully initializes its resources.

At test63's end, the finite watchdog automatically returned to OrangeFox.
The complete expdb and pstore were captured before another test. The direct
expdb record retained 7,313,360 bytes with a valid header CRC. Local raw
evidence is in ../device-cannon/test63 relative to the two repository roots;
device logs and vendor binaries stay outside Git.
