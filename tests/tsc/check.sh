#!/bin/sh
# Verifies that all E7 test programs and examples are valid strict TypeScript (using lib/e7.d.ts).
# Requires: npm install -g typescript   (or set TSC=/path/to/tsc)
set -e
cd "$(dirname "$0")"
${TSC:-tsc} -p tsconfig.json
echo "tsc: all E7 programs type-check as strict TypeScript"
