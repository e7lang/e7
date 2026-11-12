#!/bin/bash
# Builds each engine as a stripped, size-optimized shared library for Android arm64 and prints the sizes
# (see docs/COMPARISON.md).
# Usage: ANDROID_NDK_HOME=/path/to/ndk bench/compare/size.sh <lua-source-dir> <quickjs-source-dir> [hermes-android.aar]
#   lua-source-dir      a checkout of github.com/lua/lua (has onelua.c)
#   quickjs-source-dir  a checkout of github.com/bellard/quickjs
#   hermes-android.aar  the release AAR of com.facebook.hermes:hermes-android from Maven Central
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
[[ -z "$NDK" || $# -lt 2 ]] && { sed -n 2,7p "$0" >&2; exit 1; }
LUA="$(cd "$1" && pwd)"; QJS="$(cd "$2" && pwd)"; AAR="${3:-}"
BIN="$(echo "$NDK"/toolchains/llvm/prebuilt/*/bin)"
CC="$BIN/aarch64-linux-android21-clang"
OUT="$(mktemp -d)"
FLAGS="-Os -fPIC -shared -ffunction-sections -fdata-sections -Wl,--gc-sections"

(cd "$LUA" && "$CC" $FLAGS -DLUA_USE_POSIX -DLUA_USE_DLOPEN -DMAKE_LIB onelua.c -o "$OUT/liblua.so" -lm)
(cd "$QJS" && "$CC" $FLAGS -w -D_GNU_SOURCE -DCONFIG_VERSION="\"$(cat VERSION)\"" \
    quickjs.c dtoa.c libregexp.c libunicode.c cutils.c -o "$OUT/libquickjs.so" -lm)
for c in OFF ON; do
  cmake -S "$ROOT" -B "$OUT/e7-$c" -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-21 -DANDROID_STL=c++_static -DE7_BUILD_SHARED=ON \
    -DE7_WITH_COMPILER=$c -DE7_BUILD_TOOLS=OFF -DE7_BUILD_TESTS=OFF > /dev/null
  cmake --build "$OUT/e7-$c" --target e7vm > /dev/null
done
cp "$OUT/e7-OFF/libe7vm.so" "$OUT/libe7vm-runtime.so"
cp "$OUT/e7-ON/libe7vm.so" "$OUT/libe7vm-with-compiler.so"
[[ -n "$AAR" ]] && unzip -qjo "$AAR" jni/arm64-v8a/libhermesvm.so -d "$OUT"

printf "%-28s %10s %10s\n" library bytes gzip-9
for f in "$OUT"/*.so; do
  "$BIN/llvm-strip" "$f"
  printf "%-28s %10d %10d\n" "$(basename "$f")" "$(wc -c < "$f")" "$(gzip -9c "$f" | wc -c)"
done
rm -rf "$OUT"
