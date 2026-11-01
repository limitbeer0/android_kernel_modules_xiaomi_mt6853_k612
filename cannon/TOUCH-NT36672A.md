# Cannon touch bring-up, 2026-11-01

## Hardware evidence

Rec2's live 4.14 kernel reports NVTCapacitiveTouchScreen, firmware version 18,
18x36 channels and lockdown 53 42 32 01 4a 22 31 00.
SPI4 at 0x11018000 owns touch_spi@0 / touch_xsfer.
SPI5 at 0x11019000 owns fingerprint@0 / goodix_fp.
The FT8720/SPI5 conclusion in the resumed ZCode discussion is contradicted by
the live hardware. Both FTS and NVT candidate nodes/drivers exist in 4.14;
their presence alone does not identify the fitted touch controller.

## Implementation

Reuse the baseline NVT36672C driver, whose trim table includes NT36672A.
The Cannon option selects the original 65-byte protocol, normal resolution
factor 1, 1080x2340 initial input range and GPIO reset. The baseline's newer
game-tuning DT requirements and extended packet format do not apply.

The common spi-mt65xx.ko supports mt6765-spi and the original three clock names.
The signed, separately flashed stock DTBO is unchanged. A base-tree Cannon
fragment supplies GPIO14 interrupt, GPIO15 reset, SPI pinmux and verified
firmware names on the stock SPI4 child. Only Cannon accepts the old
xiaomi,spi-for-tp compatible directly; there is no old SPI transport shim.
The chip uses 5 MHz, as configured on stock /ts_novatek, even though the
legacy transport child is set to 6 MHz by the stock overlay.

The normal/MP binaries are converted from the working 4.14 reference ihex
and match full byte ranges inside the known-working boot's decompressed
kernel. Both are firmware version 18. They are embedded in the common kernel
with CONFIG_EXTRA_FIRMWARE, so loading does not depend on mounting vendor.

The modern baseline's display lockdown provider is a stub. Cannon explicitly
selects the firmware verified on this physical board via DT. It does not
fabricate a successful lockdown read. Runtime panel-ID acquisition remains
separate work; do not assume this explicit firmware selection covers other
Cannon panel variants.

## Validation

Stock DTBO index 0 overlay and board binding checks pass. Clang/LLD13 builds,
strict modpost and 15 boot tests pass.

test72 exposed two integration omissions. Common bus drivers are not hard
symbol dependencies of their SPI clients, so package.sh must explicitly root
spi-mt65xx.ko. The common controller also requires explicit CS delays: 4.14
computed setup/hold/inactive as 2*sck_time when NT36672A left them zero.
The Cannon SPI client now supplies one clock period (200 ns at 5 MHz) with
the standard spi_delay API. Without this the trim read returned invalid
80 00 00 00 00 00 / ff bytes. With it the real trim is 0a 00 00 72 66 03.

test73 (SHA256 9f8da91ddced0ad4b875a6be2e5af0fb961f17110d70aeb8588a38de3e14bf17)
boots with all 74 ko including the SPI controller, without manual insmod.
Live firmware is 18, channels 18x36, input range X0..1079/Y0..2339.
Captured 18 physical BTN_TOUCH down/up pairs plus moving X/Y coordinates;
the user confirms touch can operate the interface. This validates basic
touch, not every gesture/game/charger integration feature.

The user-supplied 98-second video shows a complete lockscreen at 69-78s,
automatic screen-off around 79s, then only backlight after power-key wakes.
This display-resume issue remains separate unfinished work. Logs show panel
status 0x0a=0x9c after resume, followed by real event410 frame-done timeouts.
Do not seed hardware events or bypass waits.

test74 additionally initializes verified firmware names before exposing
input/notifier callbacks, avoiding an early resume request with a NULL name.
It adds Cannon-only DSI/CRTC resume snapshots. Cold boot on test74 confirms
firmware 18 without the early NULL-name download error. The screen-off/wake
sequence still reproduces the display fault; normal and failed register
snapshots have been saved for diagnosis.

The required common firmware commit is a0e70971a (limitbeer0).
