# Cannon hardware bring-up, 2026-11-05

This supersedes earlier claims that enabling the generic VIBR regulator fixed
vibration. The user explicitly reported no physical vibration on test81.

## Test evidence

| Image | SHA256 | Result |
|---|---|---|
| test81 | 7d8bbbfb4ff5dbc4ca06c8f68d53c67ecb6357d3190e1dabdb4e4b89de3fd9a4 | transient LED default fixed; user still felt no vibration |
| test82 | 370bd77cc98504d5f4f90f125e66d3f35c87c3c4ccd210e10f9dbd3d108c6861 | real gauge/ADC loaded, no battery aggregator; audio binding errors |
| test83 | 192374349bd3319f673adee077948721027a89bf6af9ee30dfec32f534ab2938 | 85 modules; AW8622 loaded all 10 original waveforms at 4.030s; card0 mt6853-mt6359 registered; boot_completed=1 |
| test84 | 4fceefcd81534f5ad7b78791bc9e6d2a1e00305f9d91de985d98c1e6f2faa23a | native battery manager/kernel algorithm; battery exists, incorrect 0% and 8.7C; ADB subsequently unresponsive/offline |
| test85 | c541c6d6f97d724c6407bc9f15350b6f49a85e754659e5f1d761139e55702376 | built, NOT flashed; correct 7:2 IMP divider + original 100k NTC and 2.8V reference |

At this update the phone still contains test84. No recovery logs have been
obtained for its failure. Windows can see the ADB USB interface, but adb cannot
connect even after restarting the host server. User was asked to enter Recovery.
Do not describe the failure as a confirmed suspend deadlock without its logs.

Latest ready image is **boot-cannon-test87-final.img**, SHA256
65d789672cf689e3c09ad473d56d74c3a801148ce4d5a7561f8560304884812e.
It includes both amplifier paths, the sleepable ADC calibration callback, and
serialized access to the ADC engine. Clang13 strict modpost,15 boot tests and
stock-DTBO checks passed;86 ko and31 stock firmware blobs were extracted and
byte-verified. It is NOT flashed. Read device-cannon/test87/validation.json.
The first firmware packing patch placed files after the cpio trailer; fixed
before final image and added a roundtrip check of every payload before gzip.
Do not use intermediate test85/86/87 images for the next flash.

## Vibration

Hardware is AW8622 PWM, not a PMIC ERM motor alone. The stock HAL explicitly
supports /sys/devices/platform/haptic_pwm. Working 4.14 source is
drivers/misc/mediatek/aw8622_haptic; its memory-mode PWM waveform algorithm is
ported to 6.12 and built as aw8622_haptic.ko with native legacy mtk-pwm.ko.

Cannon uses PWM0 at 0x10048000, pin131 PWM and GPIO134 HWEN. Legacy register map
is PWM_V0; clock source mux is INFRACFG_AO +0x410, channel0 shift0, shared shift12.
Explicit DT supplier link defers haptic probe until PWM is bound.

All 24 original aw8622*.bin files were pulled from the running official vendor.
They are bundled at /lib/firmware in the boot ramdisk, so no vendor-mount timing
is needed. Synchronous firmware loading runs on the driver's ordered workqueue.
The old asynchronous shared-index callback race, double mutex-unlock in poweroff,
NULL DMA memset and missing teardown were corrected.

Test83: two 1000ms requests at 60.917s and 62.954s, actual PWM memory-mode runs
about 900ms (original duration compensation). HWEN high during playback, low
after deferred poweroff, activate=0. Physical confirmation is still pending.
Do not mark vibration working without the user's answer.

## Audio

Native 6.12 MT6853 AFE, MT6359P codec, PMIC efuse/ADC and ASoC common modules now
build and bind. Retain stock card name mt6853-mt6359 for the existing HAL.
Codec expects PMIC child name mt6359codec. Audio SRAM properties use hyphens in
6.12 instead of 4.14 underscores. Those fixes registered card0 in test83.

HAL playback reached hardware but reported missing RCV_AMP_MODE and
Ext_Receiver_Amp Switch. Original board has AW87519 on i2c6@11f00000 address0x58,
reset GPIO140, and AW87359 address0x59. Existing native baseline has neither.
Test86 uses a small ko bridge preserving original reset/register ordering and
original vendor register-pair blobs, loaded synchronously at probe. It retains
AGC, gain and boost settings, checks real chip ID0x59, and turns chips off on
error/shutdown/remove. Native machine DAPM has the two original amp paths and
receiver/speaker mode enum. Explicit supplier links defer the card until amps
are bound. No physical sound test has passed yet.

## Battery

Native mt6359p-gauge + battery core originally lacked mtk-battery-manager.o.
Cannon links the manager in the same ko, registers both drivers, and uses the
original /battery profiles through mediatek,battery-profile. The gauge's
power_supply of_node is set so the manager can resolve gauge1.

The old MIUI daemon predates the native manager's AFW netlink wrapper. Cannon
selects the native kernel algorithm via mediatek,kernel-gauge until that ABI is
ported; it does not register a misleading incompatible netlink endpoint.
Persistent gauge threads have no upstream unload teardown, so Cannon suppresses
manual bind/unbind and intentionally omits module_exit for this ko.

Test84 battery values: real voltage4308000uV, current65300uA, presence1, but
capacity0 and temperature87 (8.7C). PTIM used default unity divider, yielding
1239mV instead of the physical ~4.3V. Test85 fixes IMP ratio to7/2, matching
BATADC's actual board divider. Original normal 4.14 uses a100k NTC,100k pull-up,
2.8V reference; native default was10k/24k/1.84V. Imported the original 21-point
NTC calibration only for Cannon and added DT pull-up parameters.

Five actual original temperature curves (including 4-column T2/T3) remain in DT.
Reported CHARGE_FULL_DESIGN still follows native default QMAX_DESIGN (~2946mAh)
and requires correction against the real board/profile. Do not claim capacity
calibration or charging working yet. Charger driver is not ported.

The ADC baseline performs gauge/power_supply/regmap reads in syscore suspend,
after other CPUs/tasks/IRQs are unavailable. Test86 moves Cannon's calibration
to the sleepable PM prepare notifier and unregisters suspend hooks on teardown.
This is a source-correctness fix; test84's actual hang cause remains unconfirmed.

## Remaining hardware

No camera, fingerprint, Wi-Fi, Bluetooth or modem validation has passed.
CCCI native sources exist but are not enabled. Connectivity baseline contains
only common adapters; full WMT/BT/WLAN sources still need importing/porting.

Old normal logs identify FPC1022: Goodix probe fails, then FPC registers input.
FPC's VFP 3V rail is MT6360 LDO1, not MT6359. Need native MT6360 providers and
real FPC SPI5 clock/TEE integration. Native TEEI already has the original FPC UUID.
Do not fake fingerprint success or adopt Goodix purely from the userspace package.

Preserve test80 display/touch/resume fixes. Continue Clang/LLD13, ko in boot.
Only flash boot; do not flash independent DTBO, LK or Recovery.


## Test84 failure resolved from Recovery expdb

Recovery pstore was empty, but the 8MiB direct expdb record was valid:
732205 produced/committed bytes,160 flushes,0 dropped records. At49.608820s,
init received sys.powerctl=shutdown,battery from system_server (pid782),
then unmounted filesystems normally. This was an erroneous zero-SOC userspace
shutdown, not evidence of a kernel suspend hang. The unsafe syscore callback
found in source is an independent correctness issue.
Test87-final was subsequently flashed for the first time; device validation
is ongoing.


## Test87 real-device result (2026-11-05)

User confirmed physical vibration, correct display after wake and that sound
does exist. The quiet2s sine test was not heard, so do not claim calibrated
speaker loudness/route coverage. With original speaker routes applied,
TinyALSA played384000 bytes of48kHz/S16_LE/stereo PCM with0 bytes remaining;
both amplifiers applied original register blobs successfully.

Gauge is98%,4.283V,27.5C (later29.5C) and system boot_completed=1.
CHARGE_FULL_DESIGN still2946000uAh (template), being corrected from original
discharge curve. Battery status stillDischarging, no actual USB charging
driver at test87. User explicitly reports plugged in but not charging.

Three sysfs vibration requests activated real PWM/HWEN. Native input key
injection via sendevent event0 KEY_POWER worked to exercise off/on. Ordinary
input/cmd/dumpsys fail Binder fd transfer under diagnostic root; do not interpret
those shell errors as a failed device input test. Direct tinyplay initially
failed with no backend route, then passed after wiring ADDA_DL_CH1/2 from DL1,
LOL Playback, Ext_Speaker_Amp and a low-level test tone; route was cleaned up.
Normal shell command FD transfer needs future debug-root policy work.

Suspend entered PM prepare and PCM suspend callbacks, then aborted with MTU3
-EBUSY while USB was attached; resume completed, DRM crtc0 active=1 again.
This does not validate full USB-disconnected deep sleep.
All final/pstore/full40MiB expdb backups are in Windows test87/recovery.

## Test88/89 MT6360 and USB charging (2026-11-05)

The working 4.14 Recovery reports a real 5000000uAh design/full capacity.
The Cannon battery profile now provides that rating to the native manager;
test88/89 report it correctly, with SOC96%, around4.3V and29.5C.

Native6.12 MT6360 MFD, regulator, ADC and charger drivers are now modules.
Reuse the original signed-overlay I2C clients at0x1a/0x4e/0x64 rather than
creating duplicate dummy clients. Keep the original flat regulator nodes,
constraints and phandles, including VFP atMT6360 LDO1. The old compatible
mediatek,subpmic is accepted only on Cannon.

Test88 failed its initial0x34 PMU read with I2C NACK. The signed DTBO sets
I2C5@11d00000 to3.4MHz/push-pull. Test89 adds a Cannon-only400kHz/open-drain
override in the native adapter and successfully probes every MT6360 child.
Independent DTBO remains unchanged (da975335...); no LK/Recovery changes.
15 boot checks, Clang13/modpost, merged stock-overlay validation and ko
dependency ordering passed; test89 has92 ko and31 actual vendor firmware files.
SHA256:7e45aefb055b897b412ccd48a889cc774d2e9e6a23328c08a3770885481e1937.

Real charger registers now report ONLINE1 and Charging (and Full when idle).
ADC measured VBUS4.855V and IBUS420mA, at the original500mA SDP input limit.
Battery can still show net discharge with the screen on; do not increase
USB current without role/current negotiation. After20s screen off, charger
reported Full, IBUS142.5mA, IBAT5mA and battery current0. Wake completed.
Full recharge, USB unplug/replug, other chargers and fast charging remain untested.

Test88/89 have a finite1800s watchdog test budget and1860s expdb capture,
still automatically recovering on panic. Test88 runtime logs/full40MiB
expdb are in Windows test88/, decoded output in device-cannon/test88/.

Test90 adds a native FPC1022 resource/IRQ ko using original SPI5, GPIO47
reset, GPIO48 IRQ and MT6360 LDO1 at3V. Sysfs only appears after real HWID
0x18xx is read; TEE/HAL retain biometric processing. Currently building,
not yet flashed or validated. The old debug-tools draft is superseded.

## Test90/91 real FPC1022 (2026-11-05)

Test90 was flashed and booted. Its MTK SPI setup rejected SCK units for the
CS setup/hold delays: set_cs_timing uses spi_delay_to_ns with no transfer.
Test91 represents the original20 source ticks as200ns. Hot-loading that ko
after Android startup fixed setup but timed out during SPI access; this is
not a valid early-boot identification result (TEE had already started).

Cold boot test91 successfully read physical HWID0x1811 at3.955731s, then
registered fpc_irq@fingerprint. The root sysfs fpid_get is1 only after that
real read. GPIO48 IRQ has79 counts, SPI5 IRQ2 counts, and the original HAL
created uinput-fpc at29s. VFP supply is the original MT6360 LDO1 at3V;
reset isGPIO47 and interruptGPIO48. Do not infer full enrollment/authentication
success from these kernel/HAL initialization results. User feedback pending.

Test91 SHA2562e34b3db58ed611d9cfc182ddb0f07623c8683a2c217a53758c2ddccd24352bb,
17364992 bytes,93 ko,31 actual firmware files. It has boot_completed1 and the
MT6360 charger still online/Charging. Gauge95%,4.237V,30.7C at sample time.
Clang13/modpost,15 boot checks and signed-overlay binding validation passed.
Current device boot is91, not the older90 with its temporarily hot-loaded ko.

User subsequently confirmed "can enroll or responds to finger touch" and
MIUI now displays charging. Authentication/unlock and a full charge cycle
have not been separately confirmed. Keep these validation scopes distinct.

Camera source preparation uses native6.12 ISP6S as the framework, with five
actual Cannon O-film drivers imported from the working4.14 project folder
mt6853/camera_project/k6853v1_64_6360. They now live under
isp6s/camera_project/cannon, with original IDs, sensor register tables,
power sequences and board hardware mapping. Camera remains disabled in
Cannon config and absent from test91. Compile-only API validation is kept
outside the modules root, under device-cannon/camera-compile, so the known
boot/module ordering remains intact. Full ISP/SENINF/clock/DT/ABI integration
and camera runtime validation are still required.

## Test92–97 real camera control (2026-11-05)

Native6.12 imgsensor_isp6s now builds as a real ko with the five physical
Cannon sensor drivers and original board table/power sequences. The three
camera clock providers were already present in the baseline module set;
their DT matches and CAM15/RAWA16/RAWB17 power domains are now connected.
Physical camera buses2/4/8 use the native MT6853 adapter. SENINF retains the
18 actual original clocks, removes the legacy SCPSYS clock and uses CAM
genpd with platform mt6853/six ports. A wrong hardcoded topckgen node path
was corrected to compatible lookup; the actual node is syson@10000000.
Clock-init failures are now checked instead of exposing a clockless node.

178 shared ioctl/feature constants were compiled against both4.14 and6.12
headers: only the MAX feature-count sentinel changed3145->3152. All five
checked public payload structure sizes match. Audit: device-cannon/camera-abi/.

Test94 bound all buses/clients but returned ID0 for all sensors. Scoped
kretprobe tracing showed power functions returning0 without regulator or
pinctrl callbacks. Native hw_init cleared original board mappings when
camN_pin_* DT attributes were absent. The Cannon path now retains its
original table when those attributes are absent; other boards are unchanged.
Test95 then identified actual HI259 atCAM3, ID0x00e1.

Other camera supplies use FAN53870 on physical I2C9@11d03000, address0x35.
Native fan53870-ldo.c now has a Cannon branch that reads the real chip ID,
registers the original seven child-node constraints/phandles and applies
their original init-voltage values. It bypasses the other-board hardcoded
all-LDO reset and3.3V output policy. The physical voltage formula matches
the working4.14 driver. The Cannon consumer accepts less than8mV upward
rounding, preserving the old hardware requests (e.g.2.8V maps to2.804V),
and stops enabling a rail after a failed voltage request.

Test97 was flashed and read back with matching SHA256:
f6499985b0604026dedfe8e7bcf01729dde253a21e9178a6be0f27a97f858ec6.
17442816 bytes,95ko,31firmware; strict Clang13/modpost,15 boot tests and
signed-overlay validation passed. FAN productID0x01 and all physical IDs:
CAM0 s5kgm1stofilm f8d1; CAM1 s5k3l6ofilm30c6; CAM2 ov8856ofilm885a;
CAM3 hi259ofilm00e1. All match the working4.14 camera_info. boot_completed1,
batteryCharging31.8C and physical FPC initialized. Preview/capture is NOT
validated: native ISP frontend camera_isp/cam_qos and frame path remain.

Test96 was an unflashed incomplete artifact produced after an outer shell
continued past a build failure. It was renamed.img.incomplete on both hosts.
Always use set-e around build/package sequences. Module staging now requires
the sensor/FAN root modules whenever configured, so stale modules.order
cannot silently omit them. This commit records the test97 hardware-control baseline; preview/capture remains pending.
