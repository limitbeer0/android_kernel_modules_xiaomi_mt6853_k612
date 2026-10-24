#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
OUT=${OUT:-$ROOT/out-cannon}
LLVM_BIN=${LLVM_BIN:-/usr/lib/llvm-13/bin}
mkdir -p "$OUT"
"$LLVM_BIN/clang" --target=aarch64-linux-gnu --gcc-toolchain=/usr \
    -fuse-ld=lld -static -O2 -Wall -Wextra -Werror \
    -I"$ROOT/kernel_device_modules-6.12/include/uapi" \
    "$ROOT/cannon/ion_probe.c" -o "$OUT/cannon-ion-probe"
"$LLVM_BIN/clang" --target=armv7a-linux-gnueabi -march=armv7-a -marm \
    -fomit-frame-pointer -fuse-ld=lld -nostdlib -static -O2 -fno-builtin \
    -fno-stack-protector -Wall -Wextra -Werror -Wl,-e,_start \
    "$ROOT/cannon/ion_probe32.c" -o "$OUT/cannon-ion-probe32"
