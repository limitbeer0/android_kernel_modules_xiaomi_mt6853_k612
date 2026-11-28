#!/usr/bin/env bash
# Build native MT6853 APU power separately; it is not part of boot yet.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
CORE="$ROOT/../android_kernel_xiaomi_mt6853_k612"
VENDOR="$ROOT/kernel_device_modules-6.12"
OUT=$(printenv OUT || printf '%s' "$ROOT/out-cannon")
LLVM_BIN=$(printenv LLVM_BIN || printf '%s' /usr/lib/llvm-13/bin)
JOBS=$(printenv JOBS || printf '%s' 6)
export PATH="$LLVM_BIN:$PATH" KCONFIG_EXT_PREFIX="$VENDOR/"
make -C "$CORE" O="$OUT" ARCH=arm64 LLVM=1 LLVM_IAS=1 -j"$JOBS" \
    M="$VENDOR/drivers/misc/mediatek/apusys/power" \
    DEVICE_MODULES_PATH="$VENDOR" MTK_PLATFORM=mt6853 \
    CONFIG_MTK_APUSYS_SUPPORT=m KBUILD_EXTRA_SYMBOLS="$VENDOR/Module.symvers" \
    EXTRA_CFLAGS="-I$VENDOR/include -I$VENDOR/include/uapi -I$VENDOR/include/soc/mediatek -DCONFIG_MTK_APUSYS_SUPPORT_MODULE=1" modules
