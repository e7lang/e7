#!/usr/bin/env python3
"""Fails when Package.swift (SwiftPM) and CMakeLists.txt list different C++ sources for the library."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
cmake = (root / "CMakeLists.txt").read_text()


def cmake_list(name: str) -> set:
    m = re.search(r"set\(" + name + r"\s+(.*?)\)", cmake, re.S)
    if not m:
        sys.exit(f"cannot find {name} in CMakeLists.txt")
    return set(m.group(1).split())


want = cmake_list("E7_CORE_SRC") | cmake_list("E7_VM_SRC") | cmake_list("E7_COMPILER_SRC")

swift = (root / "Package.swift").read_text()
have = set()
for m in re.finditer(r"\[([^\[\]]*?)\]\.map \{ \"([^\"]*)\\\(\$0\)([^\"]*)\" \}", swift):
    for item in re.findall(r'"([^"]+)"', m.group(1)):
        have.add(m.group(2) + item + m.group(3))
for m in re.finditer(r'"((?:src|third_party)/[^"]+\.cpp)"', swift):
    if "\\(" not in m.group(1):  # skip the interpolated templates handled above
        have.add(m.group(1))

missing, extra = sorted(want - have), sorted(have - want)
for f in want:
    if not (root / f).exists():
        sys.exit(f"CMakeLists.txt lists a missing file: {f}")
if missing or extra:
    print("Package.swift is out of sync with CMakeLists.txt")
    if missing:
        print("  missing in Package.swift:", *missing)
    if extra:
        print("  only in Package.swift:", *extra)
    sys.exit(1)
print(f"Package.swift and CMakeLists.txt list the same {len(want)} sources")
