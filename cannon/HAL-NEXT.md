# Pending HAL compatibility checkpoint

test69 isolated the vibrator startup blocker on the phone. The GED request-size
error stopped recurring. test70 then cold booted successfully using the trigger
fix below, without manual trigger or ownership repair.

Clang/LLD 13 and strict modpost passed. The test69 package passed all 15 boot
tests, the 72-module dependency check and the original DTBO merge checks,
including the vibrator supply assertions. Prepared boot image SHA256:
4787f89bddc6b2160d1dfd549ab81a7946eb0fdd19a4a3348a6b29ee72ce59a3.

## GED monitor command 4

The stock client submits a 4-byte fd and expects a 4-byte GED error result.
The working 4.14 bridge forwards it to ged_monitor_3D_fence_add(). The newer
bridge uses a 20-byte diagnostic request, and test68 repeatedly logged the
size mismatch at frame rate. Under CONFIG_CANNON_BRINGUP the exact old 4/4
layout now calls the existing real dma-fence monitor and returns its actual
GED result. The newer diagnostic layout keeps its current handler.

## Vibrator startup blocker

The test68 Android Watchdog stack shows system_server's main thread waiting
in VibratorManagerService.nativeGetVibratorIds(). The official vibrator HAL
exits with ENODEV. Its binary uses /sys/class/leds/vibrator and writes duration
and activate; the stock kernel exposes that LED beneath
/sys/devices/platform/odm/odm:vibrator@0/leds/vibrator.

The stock DTBO supplies /odm/vibrator@0 with compatible mediatek,vibrator.
The boot DTB now supplies its regulator consumer properties, label and 2.8 V
range. The running 6.12 regulator class already reported VIBR at 2800000 uV
and disabled. verify_dt.py merges the original DTBO and checks the actual
regulator phandle, label, compatible and voltage constraints.

The baseline regulator-vibrator module gains Cannon's legacy compatible.
Its LED brightness callback schedules real regulator work. Devres teardown
unregisters notifier and LED before draining/destroying the workqueue, while
the regulator reference is still valid. Voltage is configured before
exposing the LED device.

The initial test69 patch incorrectly supplied its own duration/activate
attributes. Official /system/etc/init/hw/init.rc line 423 selects the kernel's
transient LED trigger, which already owns activate/duration/state. That
selection failed with EEXIST at 9.482376 seconds and removed the overlapping
attributes. The LED core and module field offsets were checked and agreed;
this was not a structure layout mismatch.

Re-selecting transient after the collision and applying the same system:system
ownership as stock init let the original HAL register. test69 then reached
sys.boot_completed=1, sys.user.0.ce_available=true, global Enforcing, and stopped
bootanim. No system or vendor binaries/policy were replaced for this repair.
test70 removes the duplicate attributes and custom timer; stock init and the
existing kernel transient trigger now provide the timed HAL interface.

test70 reached sys.boot_completed=1, CE=true and stopped bootanim by the
96-second snapshot. The vibrator HAL stayed running and init automatically
selected transient and applied the expected ownership. A 500 ms pulse was
tested through its sysfs interface: activate=1, brightness=255, VIBR enabled;
after the timer: activate=0, brightness=0, VIBR disabled. The sampled regulator
was identified by name as vibr and reported 2800000 uV. Physical vibration
and suspend/shutdown behavior still require confirmation.

The stock 600-second diagnostic recovery budget remains in these images.
The test70 short-power-key panic parameter was disabled at runtime for normal
UI testing; automatic panic-to-Recovery handling remains enabled. Only the
PMIC keys were enumerated as input devices, so touch is the next porting step.
