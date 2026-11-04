#!/bin/bash
# Turns the Gradle staging repository (platform/android/build/staging-repo, filled by
# `./gradlew publishAllPublicationsToStagingRepository` with signing enabled) into a Maven Central
# Portal bundle and, with --upload, uploads it via the Publisher API and waits for the result.
#
# Usage: scripts/maven-central-bundle.sh <staging-repo> <out.zip> [--upload]
# Env for --upload: MAVEN_CENTRAL_USERNAME / MAVEN_CENTRAL_PASSWORD (a Central Portal *user token*),
#                   CENTRAL_PUBLISHING_TYPE=AUTOMATIC (default) or USER_MANAGED (release by hand in the portal).
# Docs: https://central.sonatype.org/publish/publish-portal-api/
set -euo pipefail
REPO="$1"; OUT="$2"; UPLOAD="${3:-}"
[[ -d "$REPO/org" ]] || { echo "no staging repository at $REPO" >&2; exit 1; }
WORK="$(mktemp -d)"
cp -R "$REPO/org" "$WORK/"
find "$WORK" -name 'maven-metadata*' -delete
# Central requires a signature and md5/sha1 checksums for every artifact
missing=0
while IFS= read -r f; do
  for ext in asc md5 sha1; do
    [[ -f "$f.$ext" ]] || { echo "missing $f.$ext" >&2; missing=1; }
  done
done < <(find "$WORK" -type f \( -name '*.jar' -o -name '*.aar' -o -name '*.pom' -o -name '*.module' \))
[[ $missing == 0 ]] || { echo "bundle incomplete (is signing configured?)" >&2; exit 1; }
if find "$WORK" -name '*-SNAPSHOT*' | grep -q .; then echo "refusing to bundle SNAPSHOT versions" >&2; exit 1; fi
rm -f "$OUT"
OUT_ABS="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
(cd "$WORK" && zip -qr "$OUT_ABS" org)
echo "bundle: $OUT_ABS"
unzip -l "$OUT_ABS"
[[ "$UPLOAD" == "--upload" ]] || exit 0

: "${MAVEN_CENTRAL_USERNAME:?}" "${MAVEN_CENTRAL_PASSWORD:?}"
AUTH="Authorization: Bearer $(printf '%s:%s' "$MAVEN_CENTRAL_USERNAME" "$MAVEN_CENTRAL_PASSWORD" | base64 | tr -d '\n')"
API=https://central.sonatype.com/api/v1/publisher
TYPE="${CENTRAL_PUBLISHING_TYPE:-AUTOMATIC}"
ID="$(curl -sS --fail-with-body -H "$AUTH" -F "bundle=@$OUT_ABS" \
  "$API/upload?publishingType=$TYPE&name=$(basename "$OUT_ABS" .zip)")"
echo "deployment id: $ID"
for _ in $(seq 1 120); do
  STATUS="$(curl -sS --fail-with-body -X POST -H "$AUTH" "$API/status?id=$ID")"
  STATE="$(printf '%s' "$STATUS" | sed -n 's/.*"deploymentState" *: *"\([A-Z_]*\)".*/\1/p')"
  echo "state: $STATE"
  case "$STATE" in
    PUBLISHED) exit 0 ;;
    VALIDATED) [[ "$TYPE" == USER_MANAGED ]] && { echo "validated; release it at https://central.sonatype.com/publishing"; exit 0; } ;;
    FAILED) echo "$STATUS" >&2; exit 1 ;;
  esac
  sleep 15
done
echo "timed out waiting for Central (deployment $ID); check https://central.sonatype.com/publishing" >&2
exit 1
