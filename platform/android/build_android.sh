#!/bin/bash
# Builds libe7vm_jni.so for all Android ABIs with the NDK (r23+).
# Usage: ANDROID_NDK_HOME=/path/to/ndk platform/android/build_android.sh [--no-compiler]
# Output: build-android/jniLibs/<abi>/libe7vm_jni.so  -> copy to app/src/main/jniLibs/
#         Java/Kotlin API: platform/java/org/e7vm/*.java
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
[[ -z "$NDK" ]] && { echo "set ANDROID_NDK_HOME" >&2; exit 1; }
WITH_COMPILER=ON
[[ "${1:-}" == "--no-compiler" ]] && WITH_COMPILER=OFF
for ABI in arm64-v8a armeabi-v7a x86_64 x86; do
  B="$ROOT/build-android/$ABI"
  cmake -S "$ROOT" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=$ABI \
    -DANDROID_PLATFORM=android-21 -DANDROID_STL=c++_static \
    -DE7_BUILD_JNI=ON -DE7_WITH_COMPILER=$WITH_COMPILER -DE7_BUILD_TOOLS=OFF -DE7_BUILD_TESTS=OFF
  cmake --build "$B" --target e7vm_jni
  mkdir -p "$ROOT/build-android/jniLibs/$ABI"
  cp "$B/libe7vm_jni.so" "$ROOT/build-android/jniLibs/$ABI/"
done
echo "done: $ROOT/build-android/jniLibs"
