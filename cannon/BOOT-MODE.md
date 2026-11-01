# Cannon MIUI14 initial KMS mode

test64 fixed the missing legacy CRTC/plane property names, but its exact
atomic trace (`device-cannon/test64/drm-atomic-repro.txt`) records:

```
[31.850524] [CRTC:118:crtc-0] active without enabled
[31.850637] [CRTC:118:crtc-0] atomic core check failed
```

The official vendor HWC's first blankDisplay sets ACTIVE/DOZE_ACTIVE without
MODE_ID. The old 4.14 startup used fbdev to establish a KMS mode. This 6.12
configuration has no fbdev emulation; its LK first_enable configures hardware
and adjusted_mode, while the core CRTC remains disabled with no mode blob.

Under CONFIG_CANNON_BRINGUP, cannon_drm_publish_boot_mode() runs after KMS
late initialization and before drm_dev_register(). It only adopts the main
CRTC when the vendor's LK handoff marked it enabled. It locates a compatible
DSI connector, probes valid panel modes, prefers the handoff timing (otherwise
the preferred mode), and installs the connector and mode with a complete DRM
atomic transaction. All failures are propagated; EDEADLK clears the pending
transaction, backs off and retries. Connector references, state, locks and
acquire context are released on exit.

It does not fabricate a mode blob, directly overwrite core enable flags,
signal GCE EOF events, or bypass frame completion. A successful publication
log and core enabled/active state still do not prove visible output. test65
must validate the initial transaction, HWC commit, real frame completion and
phone screen separately.

Reference: Linux DRM atomic mode setter documentation:
https://www.kernel.org/doc/html/v6.9/gpu/drm-kms.html
