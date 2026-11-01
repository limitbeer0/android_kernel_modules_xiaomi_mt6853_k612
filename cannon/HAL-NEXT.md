# Pending HAL compatibility checkpoint

These changes are **built but not yet tested on the phone**. test68 remains
the user-confirmed display milestone. Do not attribute its visible output to
this subsequent patch.

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

The baseline regulator-vibrator module gains Cannon's legacy compatible and
duration/activate attributes. Activating schedules real regulator work and a
monotonic hrtimer to stop it, preserving the working driver's 25 ms short
pulse rule and 15000 ms maximum. Suspend/shutdown cancel the timer and work
before disabling the regulator. Devres teardown unregisters notifier and LED
before draining/destroying the workqueue, while the regulator reference is
still valid. Voltage is configured before exposing the LED device.

Validation remaining: probe the merged node, confirm permissions and the
official HAL registration, verify the real timed motor on/off behavior and
suspend/shutdown cleanup, then check whether system_server passes this wait.
There may be further missing HALs after it. This is not proof of a completed
Android boot or a working haptic actuator.
