#!/usr/bin/env python3
"""Performance regression gate for the benchmarks in bench/*.ts (see CONTRIBUTING.md).

Two kinds of numbers per benchmark, compared with bench/baseline.json:

* deterministic (any platform): instruction ticks (e7_ticks_used) and peak heap bytes, from
  `e7 --stats`. They depend only on the compiled bytecode and the VM's object layout, so --check
  compares them exactly: they change when the compiler, the standard library or the GC changes,
  and then the baseline is updated in the same commit (--update).
* interpreter speed (--cachegrind, Linux): machine instructions executed (Ir) under
  `valgrind --tool=cachegrind`, which is stable run to run unlike wall time. --check fails when a
  benchmark executes more than --tolerance percent (default 3) instructions more than the baseline.
  The Ir baseline is recorded for one platform (the CI runner: ubuntu-24.04 x86-64, gcc, Release);
  on other platforms the numbers are printed but not compared.

    python3 scripts/bench.py --check                     # ticks and heap (build/e7, build/e7c)
    python3 scripts/bench.py --check --cachegrind        # plus Ir (Linux, valgrind installed)
    python3 scripts/bench.py --update [--cachegrind]     # rewrite bench/baseline.json

Benchmarks are compiled with e7c first, so Ir measures loading and running, not the compiler.
"""
import argparse
import glob
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATS_RE = re.compile(
    r"\[stats ticks (\d+), gc (\d+) collections \(max pause ([\d.]+) ms, total ([\d.]+) ms\), peak heap (\d+) bytes\]")


def exe(build, name):
    for cand in (os.path.join(build, name), os.path.join(build, name + ".exe"), os.path.join(build, "Release", name + ".exe")):
        if os.path.exists(cand):
            return cand
    sys.exit(f"bench.py: {name} not found in {build} (build first, or pass --build)")


def this_platform():
    return f"{platform.system()} {platform.machine()}"


def run_stats(e7, bc):
    p = subprocess.run([e7, "--stats", bc], capture_output=True, text=True)
    m = STATS_RE.search(p.stderr)
    if p.returncode != 0 or not m:
        sys.exit(f"bench.py: {os.path.basename(bc)} failed (exit {p.returncode}):\n{p.stderr}")
    return {"ticks": int(m.group(1)), "gc_collections": int(m.group(2)), "peak_heap_bytes": int(m.group(5)),
            "gc_max_pause_ms": float(m.group(3)), "gc_total_pause_ms": float(m.group(4))}


def run_cachegrind(e7, bc, tmp):
    out = os.path.join(tmp, "cachegrind.out")
    p = subprocess.run(["valgrind", "--tool=cachegrind", "--cache-sim=no", f"--cachegrind-out-file={out}", e7, bc],
                       capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit(f"bench.py: cachegrind run of {os.path.basename(bc)} failed:\n{p.stderr[-2000:]}")
    with open(out) as f:
        for line in f:
            if line.startswith("summary:"):
                return int(line.split()[1])
    sys.exit("bench.py: no summary line in the cachegrind output")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="compare with the baseline; exit 1 on a regression")
    mode.add_argument("--update", action="store_true", help="write the measured numbers to the baseline")
    ap.add_argument("--build", default=os.path.join(ROOT, "build"), help="build directory with e7 and e7c")
    ap.add_argument("--baseline", default=os.path.join(ROOT, "bench", "baseline.json"))
    ap.add_argument("--cachegrind", action="store_true", help="also measure instructions (Ir) with valgrind")
    ap.add_argument("--tolerance", type=float, default=None, help="allowed Ir increase in percent (default: baseline's, 3)")
    ap.add_argument("--output", help="also write the measured numbers as a complete baseline file (CI artifact)")
    ap.add_argument("benchmarks", nargs="*", help="benchmark names (default: all bench/*.ts)")
    args = ap.parse_args()

    if args.cachegrind and not shutil.which("valgrind"):
        sys.exit("bench.py: --cachegrind needs valgrind")
    e7, e7c = exe(args.build, "e7"), exe(args.build, "e7c")
    try:
        with open(args.baseline) as f:
            base = json.load(f)
    except FileNotFoundError:
        base = {}
    base_bench = base.get("benchmarks", {})
    base_cg = base.get("cachegrind", {})
    base_ir = base_cg.get("ir", {})
    tolerance = args.tolerance if args.tolerance is not None else base_cg.get("tolerance_percent", 3.0)
    ir_comparable = args.cachegrind and base_cg.get("platform") == this_platform()

    sources = sorted(glob.glob(os.path.join(ROOT, "bench", "*.ts")))
    names = [os.path.splitext(os.path.basename(s))[0] for s in sources]
    if args.benchmarks:
        unknown = set(args.benchmarks) - set(names)
        if unknown:
            sys.exit(f"bench.py: unknown benchmark(s): {', '.join(sorted(unknown))}")
        names = [n for n in names if n in args.benchmarks]

    measured, ir = {}, {}
    failures, notes = [], []
    with tempfile.TemporaryDirectory() as tmp:
        hdr = f"{'benchmark':16} {'ticks':>12} {'peak heap':>12} {'GCs':>4} {'max pause':>10}"
        if args.cachegrind:
            hdr += f" {'Ir':>14} {'Ir baseline':>14} {'delta':>8}"
        print(hdr)
        for name in names:
            bc = os.path.join(tmp, name + ".e7b")
            p = subprocess.run([e7c, os.path.join(ROOT, "bench", name + ".ts"), "-o", bc], capture_output=True, text=True)
            if p.returncode != 0:
                sys.exit(f"bench.py: e7c {name}.ts failed:\n{p.stderr}")
            st = run_stats(e7, bc)
            if args.update:  # the deterministic numbers must not change between runs
                again = run_stats(e7, bc)
                for k in ("ticks", "peak_heap_bytes", "gc_collections"):
                    if again[k] != st[k]:
                        sys.exit(f"bench.py: {name}: {k} differs between two runs ({st[k]} / {again[k]})")
            measured[name] = {k: st[k] for k in ("ticks", "peak_heap_bytes", "gc_collections")}
            line = f"{name:16} {st['ticks']:>12} {st['peak_heap_bytes']:>12} {st['gc_collections']:>4} {st['gc_max_pause_ms']:>8.3f}ms"
            b = base_bench.get(name)
            if args.check:
                if b is None:
                    failures.append(f"{name}: not in the baseline")
                else:
                    for k in ("ticks", "peak_heap_bytes"):
                        if b.get(k) != st[k]:
                            failures.append(f"{name}: {k} {st[k]} != baseline {b.get(k)}")
            if args.cachegrind:
                ir[name] = run_cachegrind(e7, bc, tmp)
                bir = base_ir.get(name) if ir_comparable else None
                if bir:
                    delta = 100.0 * (ir[name] - bir) / bir
                    line += f" {ir[name]:>14} {bir:>14} {delta:>+7.2f}%"
                    if args.check and delta > tolerance:
                        failures.append(f"{name}: {ir[name]} instructions, {delta:+.2f}% over the baseline "
                                        f"(tolerance {tolerance}%)")
                    elif args.check and delta < -tolerance:
                        notes.append(f"{name}: {-delta:.1f}% fewer instructions than the baseline; update it to lock in the gain")
                else:
                    line += f" {ir[name]:>14} {'-':>14} {'':>8}"
            print(line, flush=True)

    if args.cachegrind and not ir_comparable:
        if base_cg.get("platform"):
            notes.append(f"Ir baseline is for {base_cg['platform']}, this is {this_platform()}: Ir not compared")
        else:
            notes.append("no Ir baseline yet: Ir not compared (record one with --update --cachegrind on the CI runner)")
    elif args.cachegrind:
        missing = [n for n in names if not base_ir.get(n)]
        if missing:
            notes.append(f"no Ir baseline for {', '.join(missing)}: not compared")

    def updated():
        new = {
            "_comment": "Generated by scripts/bench.py --update; see CONTRIBUTING.md (performance baseline).",
            "benchmarks": dict(base_bench, **measured),
        }
        cg = dict(base_cg)
        cg.setdefault("tolerance_percent", tolerance)
        if args.cachegrind:
            if cg.get("platform") != this_platform():
                cg["ir"] = {}
            cg["platform"] = this_platform()
            cg["ir"] = dict(cg.get("ir", {}), **ir)
        cg.setdefault("platform", None)
        cg.setdefault("ir", {})
        new["cachegrind"] = cg
        return new

    if args.output:
        with open(args.output, "w") as f:
            json.dump(updated(), f, indent=2, sort_keys=True)
            f.write("\n")
        print(f"measured numbers written to {args.output}")
    for n in notes:
        print("note:", n)
    if args.update:
        stale = args.cachegrind is False and base_ir and any(base_bench.get(k) != v for k, v in measured.items())
        with open(args.baseline, "w") as f:
            json.dump(updated(), f, indent=2, sort_keys=True)
            f.write("\n")
        print(f"updated {os.path.relpath(args.baseline, ROOT)}")
        if stale:
            print("note: ticks changed but Ir was not re-measured; take the Ir numbers from the CI perf job's "
                  "bench-baseline artifact (or run --update --cachegrind on Linux x86-64)")
        return 0
    if failures:
        print("\nperformance check FAILED:")
        for f in failures:
            print("  " + f)
        print("If the change is intended, run `python3 scripts/bench.py --update` and commit bench/baseline.json "
              "(see CONTRIBUTING.md).")
        return 1
    print("performance check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
