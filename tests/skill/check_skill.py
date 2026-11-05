#!/usr/bin/env python3
"""Checks that the E7 AI skill (skills/e7) tells the truth.

Every fenced code block in the skill's Markdown files is checked against the real compiler:
  ```ts            a complete program: must compile and run without error (e7 runs top-level code and main())
  ```ts e7-reject  TypeScript that E7 rejects: must fail to compile with a normal diagnostic (no crash)
  ```ts fragment   not checked (partial code)
Other languages (bash, kotlin, ...) are ignored. Every examples/*.ts must compile and run, and
examples/*.test.ts must pass `e7 test`. references/e7.d.ts must be identical to lib/e7.d.ts.

    check_skill.py --e7 build/e7 --e7c build/e7c [--root .]
"""
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

FENCE = re.compile(r"^```(\S*)[ \t]*([^\n]*)\n(.*?)^```[ \t]*$", re.M | re.S)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--e7", required=True)
    ap.add_argument("--e7c", required=True)
    ap.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    a = ap.parse_args()
    a.e7, a.e7c = str(pathlib.Path(a.e7).resolve()), str(pathlib.Path(a.e7c).resolve())
    root = pathlib.Path(a.root).resolve()
    skill = root / "skills" / "e7"
    failures = []
    ok = rejected = skipped = 0

    def run(cmd, cwd=None):
        return subprocess.run(cmd, capture_output=True, text=True, timeout=60, cwd=cwd)

    with tempfile.TemporaryDirectory() as tmp:
        for md in sorted(skill.rglob("*.md")):
            text = md.read_text()
            for m in FENCE.finditer(text):
                lang, tag, code = m.group(1), m.group(2).strip(), m.group(3)
                if lang not in ("ts", "typescript"):
                    continue
                line = text[: m.start()].count("\n") + 1
                where = f"{md.relative_to(root)}:{line}"
                if tag == "fragment":
                    skipped += 1
                    continue
                f = pathlib.Path(tmp, "block.ts")
                f.write_text(code)
                if tag == "e7-reject":
                    r = run([a.e7c, "--check", "--no-excerpt", str(f)])
                    if r.returncode == 0:
                        failures.append(f"{where}: expected a compile error, but it compiled")
                    elif "error:" not in r.stderr or r.returncode not in (1,):
                        failures.append(f"{where}: compiler did not fail cleanly (exit {r.returncode}): {r.stderr[:300]}")
                    else:
                        rejected += 1
                elif tag == "":
                    r = run([a.e7, "--no-excerpt", str(f)])
                    if r.returncode != 0:
                        failures.append(f"{where}: expected to compile and run, exit {r.returncode}:\n{r.stdout[-300:]}{r.stderr[-600:]}")
                    else:
                        ok += 1
                else:
                    failures.append(f"{where}: unknown block tag '{tag}' (use e7-reject or fragment)")

        ex = skill / "examples"
        for f in sorted(ex.glob("*.ts")):
            if f.name.endswith(".test.ts"):
                continue
            r = run([a.e7, "--no-excerpt", f.name], cwd=ex)
            if r.returncode != 0:
                failures.append(f"{f.relative_to(root)}: does not run (exit {r.returncode}): {r.stderr[-400:]}")
            else:
                ok += 1
        tests = sorted(ex.glob("*.test.ts"))
        if tests:
            r = run([a.e7, "test", "--no-excerpt", *[t.name for t in tests]], cwd=ex)
            if r.returncode != 0:
                failures.append(f"skills/e7/examples: e7 test failed:\n{r.stdout[-800:]}{r.stderr[-400:]}")
            else:
                ok += len(tests)

    dts_skill = skill / "references" / "e7.d.ts"
    if dts_skill.read_text() != (root / "lib" / "e7.d.ts").read_text():
        failures.append("skills/e7/references/e7.d.ts differs from lib/e7.d.ts (copy lib/e7.d.ts over it)")

    for f in failures:
        print("FAIL", f)
    print(f"skill check: {ok} programs ran, {rejected} rejections confirmed, {skipped} fragments skipped, {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
