#!/usr/bin/env python3
"""Sets the E7 version everywhere it is declared or shown, then runs check-versions.py.

    scripts/bump-version.py <new-version>

Only E7's own version strings change: in JSON files only the package's own "version" lines (a dependency that
happens to have the same version number, such as an npm package in a lock file, is left alone); the text files
listed below mention no other version numbers.
"""
import json
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent


def current() -> str:
    m = re.search(r"^project\(e7vm VERSION ([0-9.]+)", (root / "CMakeLists.txt").read_text(), re.M)
    if not m:
        sys.exit("CMakeLists.txt: version not found")
    return m.group(1)


def edit(path: str, fn) -> None:
    p = root / path
    old = p.read_text()
    new = fn(old)
    if new != old:
        p.write_text(new)
        print("updated", path)


def json_version(path: str, lock: bool = False) -> None:
    """Sets the package's own "version" in a JSON file without reformatting it: the top-level key (2-space
    indent) and, in an npm lock file, the root package's entry (`"": {...}`). Dependencies are left alone."""
    def fn(text: str) -> str:
        top = re.compile(r'^(  "version": )"' + re.escape(OLD) + '"', re.M)
        text, n = top.subn(r'\g<1>"' + NEW + '"', text, count=1)
        if n != 1:
            sys.exit(f"{path}: top-level version {OLD} not found")
        if lock:
            root_pkg = re.compile(r'(\n    "": \{[^}]*?\n      "version": )"' + re.escape(OLD) + '"')
            text, n = root_pkg.subn(r'\g<1>"' + NEW + '"', text, count=1)
            if n != 1:
                sys.exit(f"{path}: root package version {OLD} not found")
        json.loads(text)  # still valid
        return text
    edit(path, fn)


def text_version(path: str) -> None:
    """Replaces the old version (as a whole version number) in a text file that names only E7's version."""
    pat = re.compile(r"(?<![0-9.])" + re.escape(OLD) + r"(?![0-9])")
    edit(path, lambda t: pat.sub(NEW, t))


if len(sys.argv) != 2 or not re.fullmatch(r"\d+\.\d+\.\d+", sys.argv[1].lstrip("v")):
    sys.exit(__doc__)
NEW = sys.argv[1].lstrip("v")
OLD = current()
if OLD == NEW:
    sys.exit(f"already {NEW}")
major, minor, patch = NEW.split(".")

edit("CMakeLists.txt", lambda t: re.sub(r"^(project\(e7vm VERSION )[0-9.]+", r"\g<1>" + NEW, t, flags=re.M))
edit("src/capi/capi.cpp", lambda t: t.replace(f'e7_version(void) {{ return "{OLD}"', f'e7_version(void) {{ return "{NEW}"'))
def header(t: str) -> str:
    for part, val in (("MAJOR", major), ("MINOR", minor), ("PATCH", patch)):
        t = re.sub(rf"^(#define E7_VERSION_{part} )\d+", rf"\g<1>{val}", t, flags=re.M)
    return t.replace(f'e.g. "{OLD}"', f'e.g. "{NEW}"')
edit("include/e7/e7.h", header)
edit("packages/e7/index.js", lambda t: t.replace(f'export const version = "{OLD}"', f'export const version = "{NEW}"'))
json_version("packages/e7/package.json")
json_version("editors/vscode/package.json")
json_version("editors/vscode/package-lock.json", lock=True)
json_version(".claude-plugin/plugin.json")
edit("platform/dotnet/E7VM/E7VM.csproj", lambda t: t.replace(f"<Version>{OLD}</Version>", f"<Version>{NEW}</Version>")
     .replace(f"-p:Version={OLD}", f"-p:Version={NEW}"))
old_hex = "0x%02x%02x%02x" % tuple(int(x) for x in OLD.split("."))
new_hex = "0x%02x%02x%02x" % (int(major), int(minor), int(patch))
edit("docs/EMBEDDING.md", lambda t: t.replace(f"`{old_hex}`", f"`{new_hex}`"))
for path in ("docs/EMBEDDING.md", "README.md", "README.zh-CN.md", "Package.swift", "editors/vscode/README.md",
             "platform/ios/E7VM/E7VM.swift", "platform/android/build.gradle.kts", ".github/workflows/js.yml",
             ".github/workflows/maven.yml", ".github/workflows/nuget.yml"):
    text_version(path)

sys.exit(subprocess.call([sys.executable, str(root / "scripts" / "check-versions.py"), NEW]))
