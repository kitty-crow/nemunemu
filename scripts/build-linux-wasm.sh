#!/usr/bin/env bash
set -Eeuo pipefail
IFS=$'\n\t'

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LINUX_WASM="${LINUX_WASM_ROOT:-$ROOT/../linux-wasm}"
WORKSPACE="${LW_WORKSPACE:-$LINUX_WASM/workspace}"
VARIANT="${LW_VARIANT:-wasm32_nommu}"
ARCH="${VARIANT%%_*}"
LLVM="$WORKSPACE/install/llvm"
SYSROOT="$WORKSPACE/install/musl-$VARIANT"
KERNEL_HEADERS="$WORKSPACE/install/kernel-$VARIANT/include"
CC="$LINUX_WASM/tools/fake-llvm/clang"
OUT="${NEMUNEMU_WASM_OUTPUT:-$ROOT/dist/nemunemu-$VARIANT.wasm}"

need_file() {
    [[ -f "$1" ]] || {
        printf 'ERROR: Required file is missing: %s\n' "$1" >&2
        exit 1
    }
}

case "$VARIANT" in
    wasm32_nommu|wasm64_nommu)
        NEMUNEMU_MEMORY_FLAGS=(-DNEMU_NOMMU=1)
        ;;
    *)
        NEMUNEMU_MEMORY_FLAGS=()
        ;;
esac

need_file "$CC"
need_file "$LLVM/bin/clang"
need_file "$SYSROOT/lib/libc.a"
need_file "$KERNEL_HEADERS/linux/types.h"

python3 "$ROOT/scripts/generate-abi.py" --check
mkdir -p "$(dirname "$OUT")"

export REAL_LLVM="$LLVM/bin"

"$CC" \
    --target=wasm-linux-musl \
    "-march=$ARCH" \
    --sysroot="$SYSROOT" \
    -isystem "$KERNEL_HEADERS" \
    -D__linux__ \
    "${NEMUNEMU_MEMORY_FLAGS[@]}" \
    -std=c17 \
    -O2 \
    -fPIC \
    -fno-exceptions \
    -Wall -Wextra -Wpedantic -Werror \
    -I"$ROOT/include" \
    "$ROOT/src/main.c" \
    "$ROOT/src/cli.c" \
    "$ROOT/src/compat.c" \
    "$ROOT/src/thx.c" \
    "-m$ARCH" \
    -shared \
    -lm \
    -o "$OUT"

chmod 0755 "$OUT"
printf 'NEMUNEMU Linux-WASM binary: %s\n' "$OUT"
