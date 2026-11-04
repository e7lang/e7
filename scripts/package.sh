#!/usr/bin/env bash
# Packages a built tree into a release archive.
#   scripts/package.sh <build-dir> <version> <platform-name>
# Produces dist/e7-<version>-<platform>.{tar.gz|zip} containing:
#   bin/e7, bin/e7c          command-line tools
#   include/e7/e7.h          C API header
#   lib/                     static libraries for embedding
#   types/e7.d.ts            TypeScript declarations for editors / tsc
#   examples/                sample scripts + embedding example
#   README.md, LICENSE
set -euo pipefail
BUILD=${1:?build dir}
VERSION=${2:?version}
PLATFORM=${3:?platform}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NAME="e7-${VERSION}-${PLATFORM}"
STAGE="$ROOT/dist/$NAME"
rm -rf "$STAGE" && mkdir -p "$STAGE"/{bin,include/e7,lib,types,examples}

exe=""
case "$PLATFORM" in windows*) exe=".exe" ;; esac

# tools (multi-config generators put them under Release/)
for t in e7 e7c; do
  for cand in "$BUILD/$t$exe" "$BUILD/Release/$t$exe"; do
    if [ -f "$cand" ]; then cp "$cand" "$STAGE/bin/"; break; fi
  done
  [ -f "$STAGE/bin/$t$exe" ] || { echo "missing $t$exe in $BUILD" >&2; exit 1; }
done

# static libraries
for lib in e7vm_static e7_bearssl e7_monocypher; do
  for cand in "$BUILD/lib$lib.a" "$BUILD/Release/$lib.lib" "$BUILD/$lib.lib"; do
    if [ -f "$cand" ]; then cp "$cand" "$STAGE/lib/"; break; fi
  done
done

cp "$ROOT/include/e7/e7.h" "$STAGE/include/e7/"
cp "$ROOT/lib/e7.d.ts" "$STAGE/types/"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$STAGE/"
cp -R "$ROOT/examples/embed" "$STAGE/examples/embed"
for f in api_signature jwt webhook_verify license_check custom_digest; do
  cp "$ROOT/examples/$f.ts" "$STAGE/examples/"
done
rm -f "$STAGE/examples/embed/host" "$STAGE/examples/embed/"*.pub "$STAGE/examples/embed/"*.seed "$STAGE/examples/embed/"*.e7b

mkdir -p "$ROOT/dist"
cd "$ROOT/dist"
case "$PLATFORM" in
  windows*) (command -v 7z >/dev/null && 7z a -tzip "$NAME.zip" "$NAME" >/dev/null) || (cd "$ROOT/dist" && zip -qr "$NAME.zip" "$NAME"); echo "$ROOT/dist/$NAME.zip" ;;
  *) tar -czf "$NAME.tar.gz" "$NAME"; echo "$ROOT/dist/$NAME.tar.gz" ;;
esac
