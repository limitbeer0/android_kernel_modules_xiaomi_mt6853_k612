#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
OUT=$(printenv OUT || printf '%s' "$ROOT/out-cannon")
LLVM_BIN=$(printenv LLVM_BIN || printf '%s' /usr/lib/llvm-13/bin)
mkdir -p "$OUT"
for probe in isp_probe isp_ion_probe mdp_probe mdp_nop_probe dip_probe; do
    "$LLVM_BIN/clang" --target=aarch64-linux-gnu --gcc-toolchain=/usr \
        -fuse-ld=lld -static -O2 -Wall -Wextra -Werror \
        -I"$ROOT/kernel_device_modules-6.12/include/uapi" \
        -I"$ROOT/kernel_device_modules-6.12/drivers/misc/mediatek/cameraisp/src/isp_6s/inc" \
        "$ROOT/cannon/$probe.c" -o "$OUT/cannon-$probe"
done

"$LLVM_BIN/clang" --target=armv7a-linux-gnueabi -march=armv7-a -marm \
    -fomit-frame-pointer -fuse-ld=lld -nostdlib -static -O2 -fno-builtin \
    -fno-stack-protector -Wall -Wextra -Werror -Wl,-e,_start \
    "$ROOT/cannon/dip_probe32.c" -o "$OUT/cannon-dip_probe32"
