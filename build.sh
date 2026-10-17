#!/bin/bash
set -euo pipefail

MINGW_PREFIX="$(brew --prefix mingw-w64)"
SYSROOT="$MINGW_PREFIX/toolchain-i686"
MINGW_INCLUDES=("-I$SYSROOT/i686-w64-mingw32/include")

# Prefer Homebrew's LLVM if it's installed; fall back to whatever's on PATH.
if command -v brew &>/dev/null && brew --prefix llvm &>/dev/null; then
    LLVM_PREFIX="$(brew --prefix llvm)"
    CLANG="$LLVM_PREFIX/bin/clang"
    WINDRES="$LLVM_PREFIX/bin/llvm-windres"
    STRIP="$LLVM_PREFIX/bin/llvm-strip"
    OBJCOPY="$LLVM_PREFIX/bin/llvm-objcopy"
    echo "==> Using Homebrew LLVM at $LLVM_PREFIX"
else
    CLANG="${CLANG:-clang}"
    WINDRES="${WINDRES:-llvm-windres}"
    STRIP="${STRIP:-llvm-strip}"
    OBJCOPY="${OBJCOPY:-llvm-objcopy}"
    echo "==> Using system clang"
fi

COMMON=(
    --target=i686-w64-mingw32
    --sysroot="$SYSROOT"
    -Weverything -Wall -Wextra -Wpedantic -Werror -Weffc++
    -std=c++23
    -Wno-c++98-compat-pedantic
    -Wno-reserved-macro-identifier
    -Wno-lifetime-safety-intra-tu-suggestions
    -Wno-unsafe-buffer-usage
    -Wno-unused-macros
    -Wno-nonportable-system-include-path
    -fno-builtin -fno-threadsafe-statics -ftrivial-auto-var-init=pattern
    -fno-exceptions -fno-rtti -fsafe-buffer-usage-suggestions
    -flto -Oz
    -g0
    -ffunction-sections -fdata-sections
    -nostdlib -nodefaultlibs -nostartfiles
    -fuse-ld=lld
    -Wl,-e,start
    -Wl,-subsystem,windows
    -Wl,--disable-reloc-section
    -Wl,--gc-sections
)

LIBS=(-lkernel32 -luser32 -lshell32 -lole32 -loleaut32 -luuid)

echo "==> Building Relayout.exe"
"$WINDRES" "${MINGW_INCLUDES[@]}" relayout.rc -o relayout.res
"$CLANG" "${COMMON[@]}" relayout.cc relayout.res -o Relayout.exe "${LIBS[@]}"
"$STRIP" --strip-all Relayout.exe

echo "==> Done."
