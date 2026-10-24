#!/usr/bin/env bash
# Incremental Clang 13 build. Never cleans sources, installs tools or flashes.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
K=${K:-$ROOT/../android_kernel_xiaomi_mt6853_k612}
M=$ROOT/kernel_device_modules-6.12
OUT=${OUT:-$ROOT/out-cannon}
LLVM_BIN=${LLVM_BIN:-/usr/lib/llvm-13/bin}
JOBS=${JOBS:-8}
export PATH="$LLVM_BIN:$PATH"
export KCONFIG_EXT_PREFIX="$M/"
export KBUILD_BUILD_USER=cannon KBUILD_BUILD_HOST=bringup
mkdir -p "$OUT"
for tool in clang ld.lld llvm-ar llvm-nm llvm-objcopy llvm-strip; do
    test -x "$LLVM_BIN/$tool" || { echo "Missing $LLVM_BIN/$tool" >&2; exit 1; }
done
"$LLVM_BIN/clang" --version | tee "$OUT/toolchain.txt"
"$LLVM_BIN/ld.lld" --version | tee -a "$OUT/toolchain.txt"
args=(-C "$K" O="$OUT" ARCH=arm64 LLVM=1 LLVM_IAS=1)
configure() {
    make "${args[@]}" gki_defconfig
    (cd "$K"; scripts/kconfig/merge_config.sh -m -O "$OUT" \
        "$OUT/.config" "$M/arch/arm64/configs/vendor/cannon.config")
    make "${args[@]}" olddefconfig
    python3 "$ROOT/cannon/check_config.py" "$OUT/.config"
}
build_loader() {
    "$LLVM_BIN/clang" --target=aarch64-linux-gnu -fuse-ld=lld -Oz -static \
        -nostdlib -fno-builtin -fno-stack-protector -fno-unwind-tables \
        -fno-asynchronous-unwind-tables -Wl,-e,_start -Wl,--build-id=none \
        -Wall -Wextra -Werror "$ROOT/cannon/init.c" -o "$OUT/cannon-init"
}
build_dtb() {
    dtc -I dts -O dtb -o "$OUT/cannon.dtb" \
        "$M/arch/arm64/boot/dts/mediatek/cannon.dts" 2>"$OUT/cannon-dtc.log"
}
build_fscrypt_probe() {
    "$LLVM_BIN/clang" --target=aarch64-linux-gnu --gcc-toolchain=/usr \
        -fuse-ld=lld -static -O2 -Wall -Wextra -Werror \
        "$ROOT/cannon/fscrypt_probe.c" -o "$OUT/cannon-fscrypt-probe"
}
build_adbroot() {
    python3 "$ROOT/cannon/build_adbroot.py" "$OUT" "$K" --llvm "$LLVM_BIN"
}
case "${1:-all}" in
    config) configure ;;
    loader) build_loader ;;
    dtb) build_dtb ;;
    fscrypt-probe) build_fscrypt_probe ;;
    adbroot) build_adbroot ;;
    kernel) build_adbroot; make "${args[@]}" -j"$JOBS" Image.gz modules ;;
    modules)
        make "${args[@]}" -j"$JOBS" modules_prepare
        make "${args[@]}" -j"$JOBS" M="$M" DEVICE_MODULES_PATH="$M" \
            MTK_PLATFORM=mt6853 modules ;;
    all)
        configure
        build_adbroot
        make "${args[@]}" -j"$JOBS" Image.gz modules
        make "${args[@]}" -j"$JOBS" M="$M" DEVICE_MODULES_PATH="$M" \
            MTK_PLATFORM=mt6853 modules
        build_loader
        build_dtb ;;
    *) echo "Usage: $0 {config|kernel|modules|loader|dtb|fscrypt-probe|adbroot|all}" >&2; exit 2 ;;
esac
