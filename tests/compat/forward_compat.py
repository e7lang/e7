#!/usr/bin/env python3
"""Forward compatibility (docs/ROADMAP.md 0.2.5, P0.6; docs/BYTECODE.md section 5).

Modules compiled by the current compiler must run on the VM of the previous release:

  * every program is compiled with `e7c --target <base>` and run on the old e7 and on the current
    e7; exit code, stdout and stderr must be identical;
  * a program the compiler refuses for that target (it needs a feature of a newer VM) is compiled
    without --target instead, and the old VM must reject it cleanly (E7_ERR_VERSION: "the module
    requires e7vm ...", exit code 1), while the current VM runs it.

Programs with `// @error:` (compile errors) are skipped. Prints a summary (also appended as Markdown
to --summary, e.g. $GITHUB_STEP_SUMMARY); exits 1 on any difference or unclean rejection.

--allow FILE: a known difference (a bug fixed in a later VM changes that program's output on the old one);
reported, not a failure.

usage: forward_compat.py --old-e7 OLD --e7 NEW --e7c NEWC [--target 0.2] [--allow FILE]... [--summary FILE] files...
"""
import argparse, os, subprocess, sys, tempfile, zlib


def run(cmd, timeout=300):
    env = dict(os.environ, TZ="UTC")
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, env=env)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old-e7", required=True, help="e7 built from the previous release tag")
    ap.add_argument("--e7", required=True, help="current e7")
    ap.add_argument("--e7c", required=True, help="current e7c")
    ap.add_argument("--target", default="0.2", help="e7c --target value for the old VM (default 0.2)")
    ap.add_argument("--base", default="", help="name of the old release, for the report")
    ap.add_argument("--summary", help="append a Markdown summary to this file")
    ap.add_argument("--allow", action="append", default=[], help="known difference (path suffix)")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    _, old_version, _ = run([a.old_e7, "--version"])
    _, new_version, _ = run([a.e7, "--version"])
    same = rejected = 0
    allowed = []
    failures = []
    needs_newer = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, f in enumerate(a.files):
            src = open(f, encoding="utf-8").read()
            if "// @error:" in src:
                continue
            name = os.path.relpath(f)
            out = os.path.join(tmp, f"m{i}.e7b")
            rc, _, cerr = run([a.e7c, f, "-o", out, "--target", a.target])
            if rc != 0:
                # needs a newer VM: compile for the current one; the old VM must refuse it cleanly
                rc2, _, cerr2 = run([a.e7c, f, "-o", out])
                if rc2 != 0:
                    failures.append(f"{name}: does not compile: {cerr2.strip()[:300]}")
                    continue
                orc, oout, oerr = run([a.old_e7, out])
                nrc, nout, nerr = run([a.e7, out])
                if orc == 1 and "requires e7vm" in oerr and "AddressSanitizer" not in oerr:
                    rejected += 1
                    needs_newer.append(f"{name} ({cerr.strip().splitlines()[0][:160] if cerr.strip() else 'newer target'})")
                elif orc == 0 and (orc, oout, oerr) == (nrc, nout, nerr):
                    same += 1  # requires an older VM after all (the --target refusal was not a version issue)
                else:
                    failures.append(f"{name}: needs a newer VM but the old VM did not reject it with E7_ERR_VERSION "
                                    f"(exit {orc}): {oerr.strip()[:300]}")
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
        # the rejection path itself: a module whose header requires the next patch release of the old
        # VM (header field at offset 12, CRC-32 over all but the last 8 bytes, stored in the last 4)
        probe = os.path.join(tmp, "probe.ts")
        open(probe, "w").write('console.log("probe");\n')
        pout = os.path.join(tmp, "probe.e7b")
        if run([a.e7c, probe, "-o", pout, "--target", a.target])[0] == 0:
            b = bytearray(open(pout, "rb").read())
            req = int.from_bytes(b[12:16], "little")
            b[12:16] = (req + 0x10000).to_bytes(4, "little")  # one minor version newer
            b[-4:] = zlib.crc32(bytes(b[:-8])).to_bytes(4, "little")
            open(pout, "wb").write(bytes(b))
            orc, _, oerr = run([a.old_e7, pout])
            if orc != 1 or "requires e7vm" not in oerr:
                failures.append(f"a module requiring a newer VM was not rejected with E7_ERR_VERSION (exit {orc}): {oerr[:300]}")
        else:
            failures.append("cannot compile the version probe")
    lines = [
        f"Forward compatibility: modules compiled by the current e7c ({new_version.strip()}, --target {a.target}) "
        f"on {old_version.strip()}{' (' + a.base + ')' if a.base else ''}",
        f"- identical output on both VMs: {same}",
        f"- need a newer VM, rejected cleanly with E7_ERR_VERSION: {rejected}",
        f"- known differences (--allow): {len(allowed)}",
        f"- failures: {len(failures)}",
    ]
    lines += [f"  - needs newer: {n}" for n in needs_newer]
    lines += [f"  - known: {x}" for x in allowed]
    lines += [f"  - FAIL {x}" for x in failures]
    print("\n".join(lines))
    if a.summary:
        with open(a.summary, "a", encoding="utf-8") as s:
            s.write("### " + lines[0] + "\n\n" + "\n".join(lines[1:]) + "\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
