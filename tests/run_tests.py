#!/usr/bin/env python3
"""E7 test runner.

For every tests/cases/**/*.ts:
  * `// @error: <text>`          compilation must fail with a message containing <text>
  * `// @runtime-error: <text>`   compilation succeeds, execution fails with <text>
  * `<name>.out` exists           stdout must equal the file
  * otherwise                     stdout must equal Node.js output (differential test)
"""
import argparse, os, pathlib, subprocess, sys, glob, difflib

ROOT = os.path.dirname(os.path.abspath(__file__))

def run(cmd, timeout=120):
    env = dict(os.environ, TZ="UTC")  # E7 dates are always UTC
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, env=env)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--e7", default=os.path.join(ROOT, "..", "build", "e7"))
    ap.add_argument("--node", default="node")
    ap.add_argument("--update", action="store_true", help="write .out files from Node output")
    ap.add_argument("--exclude", action="append", default=[], help="skip files whose path contains this")
    ap.add_argument("filter", nargs="*")
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(ROOT, "cases", "**", "*.ts"), recursive=True))
    files = [f for f in files if not any(part.startswith("_") for part in os.path.relpath(f, ROOT).split(os.sep))]
    if a.filter:
        files = [f for f in files if any(x in f for x in a.filter)]
    files = [f for f in files if not any(x in f for x in a.exclude)]
    # --import takes a URL (a bare "D:\\..." path is rejected on Windows)
    shim = pathlib.Path(ROOT, "node_shim", "register.mjs").resolve().as_uri()
    passed = failed = 0
    for f in files:
        rel = os.path.relpath(f, ROOT)
        src = open(f, encoding="utf-8").read()
        expect_err = [l.split("@error:", 1)[1].strip() for l in src.splitlines() if "// @error:" in l]
        expect_rt = [l.split("@runtime-error:", 1)[1].strip() for l in src.splitlines() if "// @runtime-error:" in l]
        rc, out, err = run([a.e7, f])
        ok = True
        why = ""
        if expect_err:
            if rc == 0 or not all(x in err for x in expect_err):
                ok, why = False, f"expected compile error {expect_err}, got rc={rc}\n{err}"
        elif expect_rt:
            if rc == 0 or not all(x in err for x in expect_rt):
                ok, why = False, f"expected runtime error {expect_rt}, got rc={rc}\n{err}"
        else:
            if rc != 0:
                ok, why = False, f"exit code {rc}\n{err}"
            else:
                outfile = f[:-3] + ".out"
                if os.path.exists(outfile) and not a.update:
                    want = open(outfile, encoding="utf-8").read()
                else:
                    nrc, want, nerr = run([a.node, "--experimental-transform-types", "--no-warnings", "--import", shim, f])
                    if nrc != 0:
                        ok, why = False, f"node failed: {nerr}"
                    if a.update and ok:
                        open(outfile, "w", encoding="utf-8").write(want)
                if ok and out != want:
                    ok = False
                    why = "".join(difflib.unified_diff(want.splitlines(True), out.splitlines(True), "expected", "e7", n=1))[:3000]
        if ok:
            passed += 1
        else:
            failed += 1
            print(f"FAIL {rel}\n{why}")
    print(f"\n{passed} passed, {failed} failed")
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
