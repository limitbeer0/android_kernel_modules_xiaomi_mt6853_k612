#!/usr/bin/env python3
"""Fail when Kconfig silently drops boot-critical cannon settings."""
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text().splitlines()
config = dict(line.split("=", 1) for line in text if line.startswith("CONFIG_") and "=" in line)
required = {
    "CANNON_BRINGUP": "y", "ARM64_4K_PAGES": "y", "MODULES": "y",
    "SECURITY_SELINUX": "y",
    "PSTORE": "y", "PSTORE_CONSOLE": "y", "PSTORE_PMSG": "y", "PSTORE_RAM": "y",
    "BLK_DEV_INITRD": "y", "RD_GZIP": "y", "LTO_NONE": "y",
    "EXTRA_FIRMWARE": '"valhall-1691526.wa"',
    "EXTRA_FIRMWARE_DIR": '"firmware"',
    "COMMON_CLK_MT6853": "m", "PINCTRL_MT6853": "m",
    "DEVICE_MODULES_MTK_PMIC_WRAP": "m",
    "DEVICE_MODULES_MFD_MT6397": "m", "REGULATOR_MT6359P": "m",
    "CANNON_UFS_MT6853": "m", "DEVICE_MODULES_KEYBOARD_MTK_PMIC": "m",
    "CANNON_POWERKEY_PANIC": "y", "SCSI_UFSHCD": "y", "SCSI_UFSHCD_PLATFORM": "y",
    "MICROTRUST_TEE_SUPPORT": "m", "MICROTRUST_TEE_VERSION": '"400"',
    "MTK_RPMB": "m", "RPMB_INTF_DEV": "y",
    "USB_MTU3": "m", "USB_MTU3_GADGET": "y", "PHY_MTK_TPHY": "m",
}
errors = [f"CONFIG_{name}={config.get('CONFIG_'+name, 'n')} (need {value})"
          for name, value in required.items() if config.get("CONFIG_"+name) != value]
for name in ("CFI_CLANG", "SHADOW_CALL_STACK", "XAGA_MARKER_WRITER", "XAGA_KMSG2USB",
             "COMMON_CLK_MEDIATEK", "PINCTRL_MTK_PARIS", "PINCTRL_MTK_V2", "EINT_MTK",
             "MTK_PMIC_WRAP", "MFD_MT6397", "REGULATOR_MT6359",
             "SCSI_UFS_MEDIATEK", "DEVICE_MODULES_SCSI_UFS_MEDIATEK", "KEYBOARD_MTK_PMIC",
             "CMDLINE_EXTEND", "CMDLINE_FORCE", "KVM", "CPU_IDLE", "CPU_FREQ",
             "USB_MTU3_DUAL_ROLE", "USB_MTU3_HOST", "DEVICE_MODULES_USB_MTU3",
             "DEVICE_MODULES_PHY_MTK_TPHY"):
    if config.get("CONFIG_"+name) in ("y", "m"):
        errors.append(f"CONFIG_{name} must be disabled")
if config.get("CONFIG_CMDLINE") != '""':
    errors.append("CONFIG_CMDLINE must be empty; use LK/DT bootargs")
if errors:
    raise SystemExit("\n".join(errors))
print("Cannon config: boot-critical assertions passed")
