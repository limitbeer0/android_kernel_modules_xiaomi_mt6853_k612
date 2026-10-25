#!/usr/bin/env bash
# Usage: bash cannon/package.sh /absolute/path/to/stock/boot.img new-boot.img
# Kernel and modules must have been built successfully by build-cannon.sh.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
OUT=${OUT:-$ROOT/out-cannon}
DTB=${DTB:-$OUT/cannon.dtb}
LLVM_BIN=${LLVM_BIN:-/usr/lib/llvm-13/bin}
test "$#" -eq 2 || { echo "Usage: $0 stock-boot.img output.img" >&2; exit 2; }
python3 "$ROOT/cannon/check_config.py" "$OUT/.config"
python3 "$ROOT/cannon/test_boot.py"
python3 "$ROOT/cannon/verify_dt.py" "$DTB" \
    "${STOCK_DTBO:-$(dirname -- "$1")/dtbo.img}"
# Every packaging run starts with an empty staging directory. Retained for audit.
STAGE=$(mktemp -d "$OUT/modules-stage.XXXXXXXX")
python3 "$ROOT/cannon/stage_modules.py" "$OUT" \
    "$ROOT/kernel_device_modules-6.12" "$STAGE" --strip "$LLVM_BIN/llvm-strip" \
    --root phy-mtk-tphy --root mtu3
wdt_args=()
case "${WATCHDOG_TEST:-0}" in
    0) ;;
    1) wdt_args+=(--watchdog-test) ;;
    android) wdt_args+=(--watchdog-android-test) ;;
    android-long) wdt_args+=(--watchdog-android-long-test) ;;
    crypto) wdt_args+=(--crypto-probe "$OUT/cannon-fscrypt-probe") ;;
    crypto-write) wdt_args+=(--crypto-probe "$OUT/cannon-fscrypt-probe" --crypto-write) ;;
    *) echo "WATCHDOG_TEST must be 0, 1, android, android-long, crypto or crypto-write" >&2; exit 2 ;;
esac
case "${NATIVE_EXPDB:-0}" in
    0) ;;
    1) wdt_args+=(--native-expdb) ;;
    *) echo "NATIVE_EXPDB must be 0 or 1" >&2; exit 2 ;;
esac
case "${ADB_ROOT:-0}" in
    0) ;;
    1) wdt_args+=(--adb-root) ;;
    *) echo "ADB_ROOT must be 0 or 1" >&2; exit 2 ;;
esac
python3 "$ROOT/cannon/pack_boot.py" "${wdt_args[@]}" --stock "$1" \
    --kernel "$OUT/arch/arm64/boot/Image.gz" --dtb "$DTB" \
    --loader "$OUT/cannon-init" --modules "$STAGE" --output "$2"
