#!/usr/bin/env python3
"""Runs the benchmarks of bench/*.ts on e7vm and their ports (js/, lua/) on other engines.

    python3 bench/compare/run.py --qjs path/to/qjs --hermes path/to/hermes --lua path/to/lua [--runs 11]

Each program times its own work (startup and compilation are not included) and prints "... N ms".
Every engine runs every benchmark --runs times; the table shows the fastest run. A benchmark's
output must be the same on every engine. See docs/COMPARISON.md for versions and build flags.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BENCHES = ["fib", "loops", "sieve", "objects_bench", "sha256_script", "strings_bench"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--e7", default=os.path.join(ROOT, "build", "e7"))
    ap.add_argument("--qjs")
    ap.add_argument("--hermes")
    ap.add_argument("--lua")
    ap.add_argument("--node", default="node")
    ap.add_argument("--runs", type=int, default=11)
    a = ap.parse_args()

    js = os.path.join(HERE, "js", "%s.js")
    engines = [("e7vm", [a.e7], os.path.join(ROOT, "bench", "%s.ts"))]
    if a.qjs:
        engines.append(("QuickJS", [a.qjs], js))
    if a.hermes:
        engines.append(("Hermes", [a.hermes, "-O"], js))
    if a.lua:
        engines.append(("Lua", [a.lua], os.path.join(HERE, "lua", "%s.lua")))
    engines.append(("V8 --jitless", [a.node, "--jitless"], js))
    engines.append(("V8 JIT", [a.node], js))

    print("| Benchmark | " + " | ".join(e[0] for e in engines) + " |")
    print("|---|" + "---:|" * len(engines))
    ok = True
    for b in BENCHES:
        row, outputs = [], set()
        for name, cmd, pattern in engines:
            best = None
            for _ in range(a.runs):
                p = subprocess.run(cmd + [pattern % b], capture_output=True, text=True)
                line = p.stdout.splitlines()[0] if p.stdout else ""
                m = re.search(r"(\d+)\s+ms", line)
                if p.returncode != 0 or not m:
                    sys.exit(f"run.py: {name} failed on {b}:\n{p.stdout}{p.stderr}")
                ms = int(m.group(1))
                best = ms if best is None else min(best, ms)
                outputs.add(re.sub(r"\s+", " ", line[:m.start()]).strip())
            row.append(str(best))
        if len(outputs) != 1:
            ok = False
            print(f"run.py: {b}: outputs differ: {sorted(outputs)}", file=sys.stderr)
        print(f"| {b} | " + " | ".join(row) + " |")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
