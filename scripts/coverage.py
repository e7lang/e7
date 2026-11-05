#!/usr/bin/env python3
"""Line / function / region coverage of the VM, the standard library, the crypto code, the C API and
the compiler, measured over the whole ctest suite (docs/ROADMAP.md 0.2.5, P0.7).

Configures a clang build with E7_COVERAGE=ON (source-based coverage), runs ctest with every process
writing its own raw profile, merges them and writes, into the build directory:
  coverage.txt     llvm-cov report (per file)
  coverage.md      Markdown summary: totals per directory, the least covered files, the natives never
                   called by any test (also appended to --summary, e.g. $GITHUB_STEP_SUMMARY)
  coverage.lcov    lcov export (for external viewers)
  html/            annotated sources (llvm-cov show)

usage: scripts/coverage.py [--build build-cov] [--jobs N] [--summary FILE] [--no-build]
Tools: clang / clang++, llvm-profdata and llvm-cov of the same LLVM version (on macOS through xcrun;
elsewhere from PATH, or set LLVM_PROFDATA / LLVM_COV, e.g. llvm-profdata-18).
"""
import argparse, glob, os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = ["src/vm", "src/stdlib", "src/crypto", "src/capi", "src/core", "src/compiler"]


def tool(name, env):
    if os.environ.get(env):
        return [os.environ[env]]
    if sys.platform == "darwin" and shutil.which("xcrun"):
        return ["xcrun", name]
    for cand in [name] + [f"{name}-{v}" for v in range(22, 13, -1)]:
        if shutil.which(cand):
            return [cand]
    sys.exit(f"{name} not found (set {env})")


def sh(cmd, **kw):
    print("+ " + " ".join(cmd), flush=True)
    return subprocess.run(cmd, check=True, **kw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=os.path.join(ROOT, "build-cov"))
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--summary")
    ap.add_argument("--no-build", action="store_true", help="reuse an existing build and profiles")
    a = ap.parse_args()
    b = os.path.abspath(a.build)
    prof = os.path.join(b, "profiles")
    profdata = tool("llvm-profdata", "LLVM_PROFDATA")
    cov = tool("llvm-cov", "LLVM_COV")
    if not a.no_build:
        cc = os.environ.get("CC", "clang")
        cxx = os.environ.get("CXX", "clang++")
        sh(["cmake", "-S", ROOT, "-B", b, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug", "-DE7_COVERAGE=ON",
            f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}"])
        sh(["cmake", "--build", b])
        shutil.rmtree(prof, ignore_errors=True)
        os.makedirs(prof)
        env = dict(os.environ, LLVM_PROFILE_FILE=os.path.join(prof, "%p-%m.profraw"))
        # failures are reported by the regular jobs; coverage is still computed
        subprocess.run(["ctest", "--test-dir", b, "-j", str(a.jobs), "--output-on-failure"], env=env)
    raws = glob.glob(os.path.join(prof, "*.profraw"))
    if not raws:
        sys.exit("no profiles written")
    merged = os.path.join(b, "e7.profdata")
    listing = os.path.join(b, "profiles.txt")
    open(listing, "w").write("\n".join(raws) + "\n")
    sh(profdata + ["merge", "-sparse", "-f", listing, "-o", merged])
    # every instrumented executable: the library code is linked into each of them
    exes = []
    for p in [os.path.join(b, "e7"), os.path.join(b, "e7c")] + sorted(glob.glob(os.path.join(b, "tests", "*"))):
        if os.path.isfile(p) and os.access(p, os.X_OK) and not p.endswith((".py", ".txt", ".cmake")):
            exes.append(p)
    objs = [exes[0]] + [x for e in exes[1:] for x in ("-object", e)]
    ignore = ["-ignore-filename-regex", r"(third_party|/tests/|/tools/|/fuzz/|/bench/|/build[^/]*/)"]
    base = ["-instr-profile", merged] + ignore
    report = subprocess.run(cov + ["report"] + objs + base, check=True, stdout=subprocess.PIPE, text=True).stdout
    open(os.path.join(b, "coverage.txt"), "w").write(report)
    with open(os.path.join(b, "coverage.lcov"), "w") as f:
        subprocess.run(cov + ["export", "-format=lcov"] + objs + base, check=True, stdout=f)
    subprocess.run(cov + ["show", "-format=html", "-output-dir", os.path.join(b, "html"), "-show-line-counts-or-regions"]
                   + objs + base, check=True, stdout=subprocess.DEVNULL)

    # per-file rows: Filename Regions Missed Cover Functions Missed Cover Lines Missed Cover ...
    rows = []
    for line in report.splitlines():
        parts = line.split()
        if len(parts) >= 10 and parts[0].startswith(("/", "src")) and re.match(r"^\d+$", parts[1]):
            name = os.path.relpath(parts[0], ROOT) if parts[0].startswith("/") else parts[0]
            regions, rmiss, funcs, fmiss, lines, lmiss = map(int, (parts[1], parts[2], parts[4], parts[5], parts[7], parts[8]))
            rows.append((name, regions, rmiss, funcs, fmiss, lines, lmiss))
    total = [l for l in report.splitlines() if l.startswith("TOTAL")]

    def pct(miss, n):
        return f"{100.0 * (n - miss) / n:.1f}%" if n else "-"

    md = ["### Coverage (ctest, clang source-based)", ""]
    if total:
        t = total[0].split()
        md += [f"**Total**: regions {t[3]}, functions {t[6]}, lines {t[9]}", ""]
    md += ["| directory | lines | functions | regions |", "|---|---|---|---|"]
    for d in SOURCES:
        sel = [r for r in rows if r[0].startswith(d + "/")]
        if not sel:
            continue
        L = sum(r[5] for r in sel); LM = sum(r[6] for r in sel)
        F = sum(r[3] for r in sel); FM = sum(r[4] for r in sel)
        R = sum(r[1] for r in sel); RM = sum(r[2] for r in sel)
        md.append(f"| {d} | {pct(LM, L)} ({L - LM}/{L}) | {pct(FM, F)} | {pct(RM, R)} |")
    md += ["", "Least covered files (most missed lines):", "", "| file | lines | missed |", "|---|---|---|"]
    for r in sorted(rows, key=lambda r: -r[6])[:12]:
        md.append(f"| {r[0]} | {pct(r[6], r[5])} | {r[6]} |")
    # natives (E7_NATIVE functions and registered templates) never executed
    uncalled = uncalled_natives(cov, objs, base)
    md += ["", f"Natives never called by a test: {len(uncalled)}", ""]
    if uncalled:
        md.append(", ".join(f"`{n}`" for n in uncalled))
    text = "\n".join(md) + "\n"
    open(os.path.join(b, "coverage.md"), "w").write(text)
    print(report.splitlines()[-1] if report else "")
    print(text)
    if a.summary:
        with open(a.summary, "a") as s:
            s.write(text)
    return 0


def uncalled_natives(cov, objs, base):
    # Function-level report of the native library files. A native is a function with the native
    # signature (VM&, Slot*, uint32_t, Slot*, void*): E7_NATIVE(name) or a template instantiation
    # registered for one element kind (shown with its template arguments).
    files = sorted(glob.glob(os.path.join(ROOT, "src/stdlib/*.cpp")) + glob.glob(os.path.join(ROOT, "src/crypto/*.cpp")))
    out = subprocess.run(cov + ["report", "-show-functions"] + objs + base + files, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True).stdout
    mangled = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 7 and parts[1].isdigit() and parts[3].endswith("%"):
            name = parts[0].split(":")[-1]  # "file.cpp:_Z..." for static functions
            covered = parts[3] != "0.00%"
            mangled[name] = mangled.get(name, False) or covered  # several binaries list the same function
    names = [n for n, c in mangled.items() if not c]
    if not names:
        return []
    filt = shutil.which("c++filt") or shutil.which("llvm-cxxfilt")
    demangled = names
    if filt:
        demangled = subprocess.run([filt], input="\n".join(names), stdout=subprocess.PIPE, text=True).stdout.splitlines()
    sig = "(e7::VM&, e7::Slot*, unsigned int, e7::Slot*, void*)"
    res = []
    for d in demangled:
        if d.endswith(sig):
            d = d[: -len(sig)]
            res.append(re.sub(r"^void ", "", d).replace("e7::", "").replace("(Kind)", "K"))
    return sorted(set(res))


if __name__ == "__main__":
    sys.exit(main())
