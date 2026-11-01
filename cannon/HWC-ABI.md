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
