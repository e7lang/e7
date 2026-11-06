#!/usr/bin/env python3
"""Backward compatibility (docs/DESIGN-0.3.md D3, BYTECODE.md section 6.4).
Modules compiled by the compiler of an earlier release must run on the current VM unchanged:
  * every program is compiled with the old e7c and run on the old e7 and on the current e7;
    exit code, stdout and stderr must be identical;
  * programs the old compiler rejects (they use newer features) are skipped.
Programs with `// @error:` (compile errors) are skipped. Prints a summary (also appended as Markdown to
--summary, e.g. $GITHUB_STEP_SUMMARY); exits 1 on any difference.
--allow FILE: a known difference (a bug fixed in the current VM changes that program's output); reported,
not a failure.
usage: backward_compat.py --old-e7 OLD --old-e7c OLDC --e7 NEW [--allow FILE]... [--summary FILE] files...
"""
import argparse, os, subprocess, sys, tempfile


def run(cmd, timeout=300):
    env = dict(os.environ, TZ="UTC")
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, env=env)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old-e7", required=True, help="e7 built from the earlier release tag")
    ap.add_argument("--old-e7c", required=True, help="e7c built from the earlier release tag")
    ap.add_argument("--e7", required=True, help="current e7")
    ap.add_argument("--base", default="", help="name of the old release, for the report")
    ap.add_argument("--summary", help="append a Markdown summary to this file")
    ap.add_argument("--allow", action="append", default=[], help="known difference (path suffix)")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    _, old_version, _ = run([a.old_e7, "--version"])
    _, new_version, _ = run([a.e7, "--version"])
    same = skipped = 0
    allowed, failures = [], []
    with tempfile.TemporaryDirectory() as tmp:
        for i, f in enumerate(a.files):
            src = open(f, encoding="utf-8").read()
            if "// @error:" in src:
                continue
            name = os.path.relpath(f)
            out = os.path.join(tmp, f"m{i}.e7b")
            rc, _, _ = run([a.old_e7c, f, "-o", out])
            if rc != 0:
                skipped += 1  # uses a feature of a newer release
                continue
            old = run([a.old_e7, out])
            new = run([a.e7, out])
            if old == new:
                same += 1
                continue
            what = []
            if old[0] != new[0]:
                what.append(f"exit code {old[0]} (old) vs {new[0]} (new)")
            if old[1] != new[1]:
                what.append("stdout differs")
            if old[2] != new[2]:
                what.append("stderr differs: old " + repr(old[2][:200]) + " new " + repr(new[2][:200]))
            if any(name.replace(os.sep, "/").endswith(x) for x in a.allow):
                allowed.append(f"{name}: " + "; ".join(what)[:300])
            else:
                failures.append(f"{name}: " + "; ".join(what))
    lines = [
        f"Backward compatibility: modules compiled by {old_version.strip()}{' (' + a.base + ')' if a.base else ''} "
        f"on {new_version.strip()}",
        f"- identical output on both VMs: {same}",
        f"- not compiled by the old compiler (newer features): {skipped}",
        f"- known differences (--allow): {len(allowed)}",
        f"- failures: {len(failures)}",
    ]
    lines += [f"  - known: {x}" for x in allowed]
    lines += [f"  - FAIL {x}" for x in failures]
    print("\n".join(lines))
    if a.summary:
        with open(a.summary, "a", encoding="utf-8") as s:
            s.write("### " + lines[0] + "\n\n" + "\n".join(lines[1:]) + "\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
