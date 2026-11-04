# Cannon display resume

Validated on MIUI V14.0.6.0.SJECNXM (Android 12), NT36672A panel
`panel_j22_42_02_0a_vdo`, with Clang/LLD 13 and the 6.12 baseline DRM driver.

## Failure and fix

Cold boot displayed the lockscreen, but after display suspend the backlight
returned without a picture. DSI_FRAME_DONE event 410 never arrived and CMDQ
timed out. Panel power-mode reads still returned 0x9c, so that read alone did
not establish that the video path was working.

The inherited preconfiguration switched to video mode and enabled the HS clock
before `drm_panel_prepare()`. The working 4.14 MT6853 sequence instead sends the
panel initialization table in command mode with the HS clock disabled, then
selects video mode, enables HS, and starts the stream. Restore that ordering for
MT6853 while retaining the modern baseline driver and other SoCs' ordering.

Also restore the working 4.14 D-PHY CONT_DET=3 in the actually selected v1 timing
function, and its four-byte alignment of horizontal packet word counts.
These two timing corrections did not resolve the failure individually; the
panel initialization ordering is the change that restored frames in test80.

No CMDQ events are fabricated, and frame completion waits remain enabled.

## Device validation, 2026-11-04

Test80 boot SHA256:
`faa9913becf6f6410251bd309a7c6cccf0f9ce3e64dcfa0b3b221f505b149078`.

- Clang/LLD 13 build and strict modpost passed.
- Package's 15 boot tests, stock DTBO overlay checks and 74-module dependency
  validation passed. Only boot was flashed.
- `sys.boot_completed=1`, CE storage available, SELinux Enforcing.
- User confirmed that both picture and touch work after wake.
- First wake: RDMA IRQ 7811 -> 7925 over two seconds.
- Three further measured off/on cycles: brightness 0 -> 397; RDMA IRQ increments
  of 380, 382 and 383 over approximately two seconds each.
- During those measurements `enable_idlemgr:0` prevented routine IRQ gating;
  the script restored `enable_idlemgr:1` on exit. With idle management enabled,
  an unchanged IRQ count alone does not establish a stalled video stream.
- No CMDQ frame-wait timeout or ESD recovery loop appeared after the fixed
  resume in the captured verification log.

The test image retains the finite 600-second automatic Recovery window and
panic-to-Recovery behavior. It is a bring-up image, not a completed device port.

Evidence and the guarded power-key verification script are outside this repo
in `../device-cannon/test80/`. Earlier negative experiments are in test77-79.

## Reference capture caveats

The current Recovery kernel differs from the older recorded Rec2 hash. It was
not modified during this work. Its `mobile:on` / `diagnose` commands expose
registers through dmesg, but Recovery screen blanking only changes backlight
while RDMA continues scanning; it is not an Android DPMS resume test.

The known-working MIUI14 4.14.336 boot was temporarily restored and its actual
Android suspend/resume was verified, then 6.12 was flashed again. That kernel
has DEBUG_FS and DYNAMIC_DEBUG disabled and excludes clkdbg's engineering-only
`reg_read` command. No complete post-resume DSI register snapshot was obtained
from it; the legacy timing and ordering comparison uses its source.
