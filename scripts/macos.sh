#!/usr/bin/env bash
# macOS and iOS work runs on a maintainer's Mac, not on GitHub Actions.
#
#   scripts/macos.sh test                 build and test everything that needs macOS:
#                                         Release + ASan ctest, SwiftPM (macOS), the iOS simulator,
#                                         the XCFramework, and (when the tools are installed) the JNI
#                                         library with the JVM tests, the C# binding and the WebAssembly
#                                         build with the npm tests
#   scripts/macos.sh release <version>    build the macOS release files (universal e7/e7c archive, the
#                                         native libraries the jar and the NuGet package bundle), test
#                                         them, and upload them to a *draft* GitHub release v<version>
#                                         at the current commit. Then push the tag: the release workflow
#                                         builds the other platforms, packages, and publishes the release.
#   scripts/macos.sh verify <version>     after publishing: install with Homebrew from the release's formula
#
# Optional signing (release): APPLE_SIGNING_IDENTITY ("Developer ID Application: Name (TEAMID)") signs
# e7 and e7c; with APPLE_ID, APPLE_TEAM_ID and APPLE_APP_PASSWORD they are also notarized.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
[ "$(uname -s)" = Darwin ] || { echo "scripts/macos.sh runs on macOS" >&2; exit 1; }
JOBS=$(sysctl -n hw.ncpu)
UNIVERSAL=(-G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0)

step() { printf '\n==> %s\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }

find_java_home() {
  if [ -n "${JAVA_HOME:-}" ] && [ -x "$JAVA_HOME/bin/javac" ]; then echo "$JAVA_HOME"; return; fi
  /usr/libexec/java_home 2>/dev/null && return
  return 1
}

build_jni() {  # $1 = build dir; prints the path of libe7vm_jni.dylib
  local jh; jh=$(find_java_home) || { echo "no JDK found (set JAVA_HOME)" >&2; return 1; }
  JAVA_HOME="$jh" cmake -S . -B "$1" "${UNIVERSAL[@]}" -DE7_BUILD_JNI=ON -DE7_BUILD_TESTS=OFF -DE7_BUILD_TOOLS=OFF >/dev/null
  cmake --build "$1" --target e7vm_jni -j "$JOBS" >/dev/null
  find "$1" -name libe7vm_jni.dylib -print | head -1
}

ios_scheme() {
  xcodebuild -list -json > "$1/schemes.json"
  python3 -c '
import json, sys
s = json.load(open(sys.argv[1]))["workspace"]["schemes"]
print(next((x for x in s if x.endswith("-Package")), None) or ("E7VM" if "E7VM" in s else s[0]))' "$1/schemes.json"
}

cmd_test() {
  mkdir -p build-macos
  step "Release build + ctest"
  cmake -S . -B build-macos/release -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build build-macos/release -j "$JOBS" >/dev/null
  ctest --test-dir build-macos/release -j "$JOBS" --output-on-failure

  step "ASan + UBSan build + ctest"
  cmake -S . -B build-macos/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DE7_SANITIZE=ON >/dev/null
  cmake --build build-macos/asan -j "$JOBS" >/dev/null
  ctest --test-dir build-macos/asan -j "$JOBS" --timeout 3600 --output-on-failure

  step "SwiftPM (macOS)"
  swift build --scratch-path build-macos/swiftpm
  swift test --scratch-path build-macos/swiftpm

  step "iOS simulator"
  local scheme; scheme=$(ios_scheme build-macos)
  xcodebuild -scheme "$scheme" -destination 'generic/platform=iOS Simulator' -derivedDataPath build-macos/xcode build -quiet
  local udid
  udid=$(xcrun simctl list devices available -j | python3 -c '
import json, sys
d = json.load(sys.stdin)["devices"]
print(next(x["udid"] for rt, xs in sorted(d.items(), reverse=True) if "iOS" in rt for x in xs if x["name"].startswith("iPhone")))')
  echo "simulator $udid"
  xcodebuild -scheme "$scheme" -destination "id=$udid" -derivedDataPath build-macos/xcode test -quiet

  step "XCFramework"
  platform/ios/build_xcframework.sh
  test -d build-ios/e7vm.xcframework

  if find_java_home >/dev/null; then
    step "JNI library + JVM tests"
    local lib; lib=$(build_jni build-macos/jni)
    lipo -info "$lib"
    rm -rf build-macos/natives && mkdir -p build-macos/natives/macos && cp "$lib" build-macos/natives/macos/
    (cd platform/android && JAVA_HOME=$(find_java_home) ./gradlew --console=plain -Pe7.jvmOnly=true \
      "-Pe7.nativesDir=$ROOT/build-macos/natives" :jvm:test)
  else
    echo "skipped: JNI / JVM tests (no JDK; set JAVA_HOME)"
  fi

  if have dotnet; then
    step "C# binding"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DE7_BUILD_SHARED=ON -DE7_BUILD_TESTS=OFF -DE7_BUILD_TOOLS=OFF >/dev/null
    cmake --build build --target e7vm -j "$JOBS" >/dev/null
    DOTNET_ROLL_FORWARD=Major dotnet run --project platform/windows/test -c Release -- build
  else
    echo "skipped: C# binding (no dotnet)"
  fi

  if have emcc; then
    step "WebAssembly + npm tests"
    emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build build-wasm -j "$JOBS" >/dev/null
    node tests/wasm/smoke.mjs build-wasm/e7wasm.js
    cp build-wasm/e7wasm.js build-wasm/e7wasm.wasm packages/e7/wasm/
    (cd packages/e7 && npm test)
  else
    echo "skipped: WebAssembly / npm tests (no emcc: brew install emscripten)"
  fi
  step "macOS checks passed"
}

sign_and_notarize() {
  [ -n "${APPLE_SIGNING_IDENTITY:-}" ] || { echo "not signed (APPLE_SIGNING_IDENTITY not set)"; return; }
  for b in "$@"; do
    codesign --force --options runtime --timestamp --sign "$APPLE_SIGNING_IDENTITY" "$b"
    codesign --verify --strict --verbose=2 "$b"
  done
  if [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ] && [ -n "${APPLE_APP_PASSWORD:-}" ]; then
    for b in "$@"; do
      ditto -c -k --keepParent "$b" "$b.zip"
      xcrun notarytool submit "$b.zip" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" --wait
      rm -f "$b.zip"
    done
  else
    echo "not notarized (APPLE_ID / APPLE_TEAM_ID / APPLE_APP_PASSWORD not set)"
  fi
}

cmd_release() {
  local version=${1:?usage: scripts/macos.sh release <version>}
  version=${version#v}
  local tag="v$version"
  python3 scripts/check-versions.py "$version"
  git diff --quiet && git diff --cached --quiet || { echo "commit your changes first" >&2; exit 1; }
  local sha; sha=$(git rev-parse HEAD)
  git fetch -q origin
  git merge-base --is-ancestor "$sha" origin/main || { echo "push $sha to origin first" >&2; exit 1; }
  if git ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null; then
    echo "tag $tag already exists on origin: run this before pushing the tag" >&2; exit 1
  fi

  local out=build-macos/release-$version
  rm -rf "$out" && mkdir -p "$out/dist"

  step "Universal e7 / e7c (Release) + ctest"
  cmake -S . -B "$out/build" "${UNIVERSAL[@]}" >/dev/null
  cmake --build "$out/build" -j "$JOBS" >/dev/null
  ctest --test-dir "$out/build" -j "$JOBS" --output-on-failure
  sign_and_notarize "$out/build/e7" "$out/build/e7c"

  step "Archive"
  local archive; archive=$(scripts/package.sh "$out/build" "$version" macos-universal)
  local d="dist/e7-$version-macos-universal"
  "$d/bin/e7" --version
  local want=0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6
  [ "$("$d/bin/e7" examples/embed/sign.ts)" = "$want" ] || { echo "smoke test failed (source)" >&2; exit 1; }
  "$d/bin/e7c" examples/embed/sign.ts -o "$out/smoke.e7b"
  [ "$("$d/bin/e7" "$out/smoke.e7b")" = "$want" ] || { echo "smoke test failed (bytecode)" >&2; exit 1; }
  lipo -info "$d/bin/e7"
  mv "$archive" "$out/dist/"
  (cd "$out/dist" && shasum -a 256 "$(basename "$archive")" > "$(basename "$archive").sha256" && cat ./*.sha256)

  step "Native libraries for the jar (JNI) and the NuGet package"
  local stage="$out/natives"
  mkdir -p "$stage/natives/macos" "$stage/native/osx"
  cp "$(build_jni "$out/jni")" "$stage/natives/macos/"
  cmake -S . -B "$out/shared" "${UNIVERSAL[@]}" -DE7_BUILD_SHARED=ON -DE7_BUILD_TESTS=OFF -DE7_BUILD_TOOLS=OFF >/dev/null
  cmake --build "$out/shared" --target e7vm -j "$JOBS" >/dev/null
  cp "$out/shared/libe7vm.dylib" "$stage/native/osx/"
  lipo -info "$stage/natives/macos/libe7vm_jni.dylib" "$stage/native/osx/libe7vm.dylib"
  tar -czf "$out/dist/e7-$version-macos-natives.tar.gz" -C "$stage" natives native

  step "Draft release $tag at $sha"
  if ! gh release view "$tag" >/dev/null 2>&1; then
    gh release create "$tag" --draft --target "$sha" --title "E7 $tag" \
      --notes "Draft: macOS files built locally (scripts/macos.sh). Pushing the tag $tag runs the release workflow, which adds the other platforms and publishes this release."
  fi
  gh release upload "$tag" "$out"/dist/* --clobber
  gh release view "$tag" --json isDraft,assets --jq '"draft=\(.isDraft) assets=\([.assets[].name] | join(", "))"'
  echo
  echo "next: git tag -a $tag -m \"E7 $tag\" $sha && git push origin $tag"
}

cmd_verify() {
  local version=${1:?usage: scripts/macos.sh verify <version>}
  local tag="v${version#v}" tmp; tmp=$(mktemp -d)
  gh release download "$tag" -p e7.rb -D "$tmp"
  brew tap-new --no-git e7lang/verify 2>/dev/null || true
  cp "$tmp/e7.rb" "$(brew --repository e7lang/verify)/Formula/e7.rb"
  HOMEBREW_NO_AUTO_UPDATE=1 brew reinstall e7lang/verify/e7 || HOMEBREW_NO_AUTO_UPDATE=1 brew install e7lang/verify/e7
  brew test e7lang/verify/e7
  "$(brew --prefix)/bin/e7" --version
  printf 'export function main(): number { return 6 * 7; }\n' > "$tmp/t.ts" && "$(brew --prefix)/bin/e7" "$tmp/t.ts"
}

case "${1:-}" in
  test) cmd_test ;;
  release) shift; cmd_release "$@" ;;
  verify) shift; cmd_verify "$@" ;;
  *) sed -n '2,16p' "$0"; exit 2 ;;
esac
