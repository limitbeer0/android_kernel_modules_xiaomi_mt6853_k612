# Cannon's legacy HWC plane blend ABI

The MIUI 14 vendor HWC asks for `PLANE_PROP_ALPHA_CON` and
`PLANE_PROP_PLANE_ALPHA`. The 6.12 driver only exposed standard DRM alpha
and pixel-blend properties, so these names were missing during HWC plane
initialization. The compatibility properties are enabled only by
`CONFIG_CANNON_BRINGUP`; the standard DRM properties remain available.

- `PLANE_PROP_PLANE_ALPHA` retains the 0..255 range and default 255. Its
  setter writes `drm_plane_state.alpha = value * 257`; its getter reads the
  standard state's top byte, so both names describe the same plane alpha.
- `PLANE_PROP_ALPHA_CON` retains the 0..1 range and default 1. The actual
  OVL CON bit 8 consumes it, including the old opaque XRGB/XBGR special case.
- Using either legacy property selects the old framebuffer blend convention:
  the `MTK_FMT_PREMULTIPLIED` modifier reaches the format converter and OVL's
  CONST_BLD bit remains clear, as in the working 4.14 driver. A plane that has
  never selected this legacy ABI keeps the existing 6.12 blend-mode behavior.
  The selection survives duplicate-state transactions until plane reset;
  writing a standard property afterwards does not implicitly change its ABI.
- Reset and duplicate-state paths initialize and preserve these values even
  when the general vendor property-inheritance option is disabled.

## Disabled-only CRTC compatibility

The official `hwcomposer.mt6853.so` has been inspected directly. Its
`DrmObject::checkProperty()` at ELF virtual address `0xe8d54` checks every
expected property's `hasInit()` flag. Any missing property sets its return
value to `-22` at `0xe8de0`, without an optional-property exemption. Both
`DrmModeCrtc::init()` and `DrmModePlane::init()` return that failure (the check
dispatch sites are `0xe3818` and `0xe4c90`). The test63 log reports failed initialization of
all CRTC and plane objects, followed by atomic commit `-EINVAL`; these are not
merely harmless discovery warnings.

Cannon therefore also exposes `ICON_ENABLE`, `ENROLL_ENABLE` and `SKIP_CONFIG`
with a deliberately restricted range **0..0**, default 0. These fields only
represent the disabled state required for compatibility with HWC discovery.
They do not advertise support for under-display fingerprint rendering or
configuration skipping. A request to enable any of them is rejected by the
DRM property's range validation; the driver setter also rejects nonzero input
defensively. Normal hardware configuration and frame synchronization remain
active when the accepted value 0 is supplied.

The supplied 4.14 source has no consumers for ICON/ENROLL and no definition of
SKIP_CONFIG. Enabling such features needs separate, verified implementations;
the compatibility layer must not silently accept nonzero values.

test64 built with Clang 13 and strict modpost, passed the boot-package checks,
and ran on the phone. HWC's missing-property and all-CRTC/plane-init failures
disappeared. Its first blankDisplay still returned EINVAL. A composer restart
under DRM atomic tracing captured `active without enabled` from the core,
which is a separate initial MODE_ID problem (see BOOT-MODE.md).

Successful frame commits and OVL alpha/premultiply register values with
opaque, translucent and premultiplied layers remain unverified. Also verify
that nonzero requests for the disabled-only properties fail. Property
discovery alone does not prove display output.

## DIM private modifier compatibility (2026-11-20)

MIUI also attaches the old private blend hint 0x0a00000000000000/1 to its
C8/DIM framebuffer. The existing framebuffer compatibility path normalized
the hint for RGB formats only. A C8 hint consequently remained in
drm_framebuffer.modifier and the generic DRM plane-format check rejected
the whole atomic request with -EINVAL.

A private kprobe trace on test110 captured this exact sequence at uptime
490.109544: drm_plane_has_format(C8, 0x0a00000000000001) returned false,
then drm_atomic_check_only and drm_mode_atomic_ioctl returned -22.
camera_isp was not loaded in that image. The symptom can therefore occur
before ISP frontend code is present; stopping display IRQs alone had not
identified the rejection.

Cannon now also accepts C8 in cannon_fb_has_legacy_blend_hint(). The hint
stays in legacy_blend_modifier for OVL's old blend semantics, while KMS sees
LINEAR. C8/DIM has no pixel GEM object in this driver. The private tag is
not advertised as a modern vendor-specific pixel layout, and other formats
or unrelated modifiers retain their previous validation.

test116 used test110's unchanged Image and DTBs, replacing only DRM in the
ramdisk. Its DIM framebuffer reports modifier 0. The user confirmed sustained
picture, touch, DIM/menu interaction and screen off/on. test117 retains
test113's full ISP/QoS frontend (105 modules) with the same DRM replacement;
the user also confirmed desktop and wake work after opening and leaving the
camera. Camera preview remains unavailable and requires separate debugging.
This test does not establish the outstanding OVL register-value checks above.

The DRM module was rebuilt with Clang 13 and strict modpost; its 574 imports,
99 exports and vermagic match the old module. Image, kernel version, DTB and
independent signed DTBO remain unchanged. Runtime debug is restored to 0
and temporary private kprobe groups are removed after capture.
