#!/usr/bin/env python3
"""Fails unless every place that declares the E7 version agrees with CMakeLists.txt.

    scripts/check-versions.py [expected-version]
"""
import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent


def grab(path: str, pattern: str) -> str:
    m = re.search(pattern, (root / path).read_text(), re.M)
    if not m:
        sys.exit(f"{path}: version not found ({pattern})")
    return m.group(1)


found = {
    "CMakeLists.txt": grab("CMakeLists.txt", r"^project\(e7vm VERSION ([0-9.]+)"),
    "src/capi/capi.cpp (e7_version)": grab("src/capi/capi.cpp", r'e7_version\(void\) \{ return "([^"]+)"'),
    "packages/e7/package.json": json.loads((root / "packages/e7/package.json").read_text())["version"],
    "packages/e7/index.js": grab("packages/e7/index.js", r'^export const version = "([^"]+)"'),
    "editors/vscode/package.json": json.loads((root / "editors/vscode/package.json").read_text())["version"],
    ".claude-plugin/plugin.json": json.loads((root / ".claude-plugin/plugin.json").read_text())["version"],
    "platform/dotnet/E7VM/E7VM.csproj": grab("platform/dotnet/E7VM/E7VM.csproj", r"<Version>([^<]+)</Version>"),
}
want = sys.argv[1].lstrip("v") if len(sys.argv) > 1 else found["CMakeLists.txt"]
bad = {k: v for k, v in found.items() if v != want}
for k, v in found.items():
    print(f"{'ok ' if v == want else 'BAD'} {v:10} {k}")
if bad:
    sys.exit(f"version mismatch: expected {want} everywhere")
