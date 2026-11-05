#!/usr/bin/env python3
"""Checks the TypeScript feature samples (tests/ts-features) against the E7 compiler and Node.js.

Every sample is a small TypeScript program about one feature, with a header:

  // @feature: <short name>
  // @category: <handbook chapter, one of CATEGORIES>
  // @status: supported | unsupported | partial
  // @why: <principle>: <detail>         (unsupported and partial: which E7 principle excludes it)
  // @instead: <what to write in E7>     (unsupported and partial)
  // @error: <substring of the E7 error> (unsupported, and the .no.ts file of a partial feature)
  // @note: <optional remark for the table>

Rules:
  * supported: `e7 <file>` succeeds and prints exactly what Node.js prints (with tests/node_shim);
  * unsupported: `e7c --check <file>` fails with a message containing @error;
  * partial: two files <name>.ok.ts (checked like supported) and <name>.no.ts (checked like unsupported)
    with the same @feature;
  * every sample must also pass `tsc --strict` (tests/ts-features/tsconfig.json; run with --tsc, and in CI by
    tests/tsc/check.sh).

Files and directories whose name starts with "_" are helpers (imported modules), not samples.
scripts/gen-ts-features.py turns the headers into docs/TYPESCRIPT.md.
"""
import argparse, concurrent.futures, difflib, glob, os, pathlib, re, subprocess, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(ROOT, "..", ".."))

# handbook order (https://www.typescriptlang.org/docs/handbook/), then the JavaScript a TypeScript program uses
CATEGORIES = [
    "Everyday Types", "Narrowing", "More on Functions", "Object Types", "Generics", "Type Manipulation",
    "Classes", "Modules", "Enums", "Utility Types", "Decorators", "Declaration Merging",
    "Iterators and Generators", "Symbols", "Mixins", "Triple-Slash Directives", "JSX", "Namespaces",
    "Type Compatibility and Inference", "Variable Declaration", "JavaScript: Syntax", "JavaScript: Standard Library",
]
# the E7 principles a @why line starts with (docs/TYPESCRIPT.md explains them)
PRINCIPLES = [
    "types known at compile time", "verifiable bytecode", "JavaScript-identical behaviour", "not implemented yet",
]
STATUSES = ["supported", "unsupported", "partial"]
KEYS = ["feature", "category", "status", "why", "instead", "error", "note"]


class Sample:
    def __init__(self, path, root=ROOT):
        self.path = path
        self.rel = os.path.relpath(path, root).replace(os.sep, "/")
        self.meta = {}
        self.problems = []
        lines = open(path, encoding="utf-8").read().splitlines()
        if lines and lines[0].startswith("#!"):
            lines = lines[1:]
        for line in lines:
            m = re.match(r"^// @(\w+):\s*(.*?)\s*$", line)
            if not m:
                break
            key, value = m.group(1), m.group(2)
            if key not in KEYS:
                self.problems.append(f"unknown header key @{key}")
            elif key in self.meta:
                self.problems.append(f"duplicate header key @{key}")
            else:
                self.meta[key] = value
        name = os.path.basename(path)
        self.part = "ok" if re.search(r"\.ok\.tsx?$", name) else "no" if re.search(r"\.no\.tsx?$", name) else None
        # how the file is checked
        status = self.meta.get("status")
        self.expect = ("unsupported" if status == "unsupported" or self.part == "no" else "supported")

    @property
    def feature(self):
        return self.meta.get("feature", "")

    @property
    def status(self):
        return self.meta.get("status", "")


def load(root=ROOT):
    """All samples (helpers excluded), sorted by path."""
    files = glob.glob(os.path.join(root, "**", "*.ts"), recursive=True) + glob.glob(os.path.join(root, "**", "*.tsx"), recursive=True)
    out = []
    for f in sorted(files):
        rel = os.path.relpath(f, root)
        if any(part.startswith("_") for part in rel.split(os.sep)):
            continue
        out.append(Sample(f, root))
    return out


def validate(samples):
    """Header rules; returns a list of (rel, problem)."""
    probs = [(s.rel, p) for s in samples for p in s.problems]
    by_feature = {}
    for s in samples:
        def bad(msg):
            probs.append((s.rel, msg))
        for key in ("feature", "category", "status"):
            if not s.meta.get(key):
                bad(f"missing @{key}")
        if s.meta.get("category") and s.meta["category"] not in CATEGORIES:
            bad(f"unknown @category '{s.meta['category']}' (one of: {', '.join(CATEGORIES)})")
        if s.status and s.status not in STATUSES:
            bad(f"@status must be one of {STATUSES}")
        if s.status == "partial" and s.part is None:
            bad("a partial feature is two files, <name>.ok.ts and <name>.no.ts")
        if s.status in ("supported", "unsupported") and s.part is not None:
            bad(f"only partial features use .ok.ts / .no.ts files (this one is {s.status})")
        if s.expect == "unsupported":
            for key in ("why", "instead", "error"):
                if not s.meta.get(key):
                    bad(f"missing @{key} (required for {'unsupported' if s.status == 'unsupported' else 'the .no.ts file of a partial'} feature)")
        else:
            for key in ("error", "why", "instead"):
                if key in s.meta:
                    bad(f"@{key} belongs to unsupported samples (or the .no.ts file of a partial feature)")
        why = s.meta.get("why")
        if why and not any(why == p or why.startswith(p + ":") for p in PRINCIPLES):
            bad("@why must start with one of: " + "; ".join(f"'{p}'" for p in PRINCIPLES) + " (optionally followed by ': detail')")
        if s.feature:
            by_feature.setdefault(s.feature, []).append(s)
    for feature, group in by_feature.items():
        rels = ", ".join(x.rel for x in group)
        if any(x.status == "partial" for x in group):
            parts = sorted(x.part or "" for x in group)
            same_stem = len({re.sub(r"\.(ok|no)\.tsx?$", "", x.rel) for x in group}) == 1
            if parts != ["no", "ok"] or not same_stem or any(x.status != "partial" for x in group):
                probs.append((rels, f"partial feature '{feature}' needs exactly <name>.ok.ts and <name>.no.ts, both @status: partial"))
            cats = {x.meta.get("category") for x in group}
            if len(cats) > 1:
                probs.append((rels, f"the files of '{feature}' have different categories"))
        elif len(group) > 1:
            probs.append((rels, f"duplicate @feature '{feature}'"))
    return probs


def run(cmd, timeout=120):
    env = dict(os.environ, TZ="UTC")  # E7 dates are always UTC
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, env=env)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def run_node(node, shim, f):
    # as tests/run_tests.py: type stripping first, the transform only for parameter properties and enums
    r = run([node, "--experimental-strip-types", "--no-warnings", "--import", shim, f])
    if r[0] != 0 and ("ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX" in r[2] or "ERR_INVALID_TYPESCRIPT_SYNTAX" in r[2]):
        r = run([node, "--experimental-transform-types", "--no-warnings", "--import", shim, f])
    return r


def check_one(s, a, shim):
    """Returns an error text, or None."""
    if s.expect == "unsupported":
        rc, out, err = run([a.e7c, "--check", "--no-excerpt", s.path])
        if rc == 0:
            return "E7 accepts this sample, but it is marked unsupported (update @status, or move it to a .ok.ts file)"
        want = s.meta.get("error", "")
        if want not in err:
            return f"expected an E7 compile error containing {want!r}, got:\n{err.strip()}"
        if "internal error" in err.lower():
            return f"internal compiler error:\n{err.strip()}"
        return None
    rc, out, err = run([a.e7, s.path])
    if rc != 0:
        return f"E7 rejects this supported sample (exit code {rc}):\n{err.strip()}"
    nrc, want, nerr = run_node(a.node, shim, s.path)
    if nrc != 0:
        return f"node failed:\n{nerr.strip()}"
    if out != want:
        diff = "".join(difflib.unified_diff(want.splitlines(True), out.splitlines(True), "node", "e7", n=1))
        return "output differs from Node.js:\n" + diff[:3000]
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--e7", default=os.path.join(REPO, "build", "e7"))
    ap.add_argument("--e7c", default=os.path.join(REPO, "build", "e7c"))
    ap.add_argument("--node", default="node")
    ap.add_argument("--tsc", help="also type-check every sample with this tsc (tests/ts-features/tsconfig.json)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("filter", nargs="*", help="only samples whose path contains one of these")
    a = ap.parse_args()

    samples = load()
    problems = validate(samples)
    for rel, p in problems:
        print(f"FAIL {rel}: {p}")
    if a.filter:
        samples = [s for s in samples if any(x in s.rel for x in a.filter)]
    shim = pathlib.Path(REPO, "tests", "node_shim", "register.mjs").resolve().as_uri()
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for s, why in zip(samples, pool.map(lambda s: check_one(s, a, shim), samples)):
            if why:
                failed += 1
                print(f"FAIL {s.rel} ({s.expect})\n{why}\n")
    if a.tsc:
        rc, out, err = run([a.tsc, "-p", os.path.join(ROOT, "tsconfig.json")], timeout=600)
        if rc != 0:
            failed += 1
            print(f"FAIL tsc --strict:\n{out}{err}")
    counts = {st: len({s.feature for s in samples if s.status == st}) for st in STATUSES}
    print(f"\n{len(samples) - failed} of {len(samples)} sample files passed, {len(problems)} header problems; "
          f"features: {counts['supported']} supported, {counts['partial']} partial, {counts['unsupported']} unsupported")
    return 1 if failed or problems else 0


if __name__ == "__main__":
    sys.exit(main())
