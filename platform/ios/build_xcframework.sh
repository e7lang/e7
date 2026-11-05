#!/bin/bash
# Builds build-ios/e7vm.xcframework (iOS device arm64 + simulator arm64/x86_64).
# Usage: platform/ios/build_xcframework.sh [--no-compiler]
# Requires macOS with Xcode and CMake >= 3.16. Then add the xcframework and
# platform/ios/E7VM/E7VM.swift (and E7ModuleManager.swift) to your app target and `import CE7VM` is available.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build-ios"
WITH_COMPILER=ON
[[ "${1:-}" == "--no-compiler" ]] && WITH_COMPILER=OFF
rm -rf "$OUT"
build() { # dir sysroot archs
  cmake -S "$ROOT" -B "$OUT/$1" -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$2" \
    -DCMAKE_OSX_ARCHITECTURES="$3" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DE7_WITH_COMPILER=$WITH_COMPILER -DE7_BUILD_TOOLS=OFF -DE7_BUILD_TESTS=OFF
  cmake --build "$OUT/$1" --config Release -- -quiet
  # merge e7vm and its dependencies into a single static library
  libtool -static -o "$OUT/$1/libe7vm_all.a" $(find "$OUT/$1" -name "*.a" -path "*Release*" ! -name "libe7vm_all.a")
}
build device iphoneos arm64
build sim iphonesimulator "arm64;x86_64"
HDR="$OUT/headers"
mkdir -p "$HDR"
cp "$ROOT/include/e7/e7.h" "$ROOT/platform/ios/module.modulemap" "$HDR/"
xcodebuild -create-xcframework \
  -library "$OUT/device/libe7vm_all.a" -headers "$HDR" \
  -library "$OUT/sim/libe7vm_all.a" -headers "$HDR" \
  -output "$OUT/e7vm.xcframework"
echo "built $OUT/e7vm.xcframework"
