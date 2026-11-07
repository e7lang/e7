#!/usr/bin/env python3
"""Writes docs/TYPESCRIPT.md, the TypeScript feature coverage table, from the samples in tests/ts-features.

  python3 scripts/gen-ts-features.py           # regenerate docs/TYPESCRIPT.md
  python3 scripts/gen-ts-features.py --check   # fail if docs/TYPESCRIPT.md is not up to date (CI)

The samples carry their status in a header (see tests/ts-features/check.py, which also verifies them against
the compiler, Node.js and tsc). This script only reads the headers; it does not run anything.
"""
import argparse, importlib.util, json, os, re, sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
FEATURES = os.path.join(REPO, "tests", "ts-features")
OUT = os.path.join(REPO, "docs", "TYPESCRIPT.md")

sys.dont_write_bytecode = True  # no __pycache__ in tests/ts-features
spec = importlib.util.spec_from_file_location("ts_features_check", os.path.join(FEATURES, "check.py"))
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)

PRINCIPLE_TEXT = {
    "types known at compile time":
        "**Types known at compile time.** Every value has one static type, so the compiler chooses its "
        "representation (a double, a machine integer, a fixed object layout) and the bytecode carries no run-time "
        "type tags. Constructs whose type is only known at run time (`any`, `unknown`, unions of unrelated types, "
        "index signatures) or that compute types from types (`keyof`, conditional and mapped types) are left out.",
    "verifiable bytecode":
        "**Verifiable bytecode.** Modules are compiled ahead of time and verified when they are loaded; the code "
        "and the object layouts it was verified against cannot change afterwards. `eval`, dynamic `import()`, "
        "decorators, `Proxy`, prototype manipulation and ambient values other than host functions are left out.",
    "JavaScript-identical behaviour":
        "**Behaviour identical to JavaScript.** An E7 program prints exactly what Node.js prints. A feature whose "
        "result would differ (it depends on garbage collection timing or on locale data, or JavaScript would "
        "silently misbehave) is rejected at compile time rather than approximated. The few run-time differences "
        "that remain (out-of-bounds reads are errors, for example) are listed in "
        "[SPEC.md section 9](SPEC.md#9-known-differences-from-javascript-summary).",
    "small runtime":
        "**Small runtime.** e7vm is embedded in apps, so every feature's code and data ship with each of them. "
        "A feature that needs a large engine or large tables (regular expressions with their Unicode data) "
        "is left out; string methods cover the common cases.",
    "not implemented yet":
        "**Not implemented yet.** The feature fits the principles above but the compiler does not support it "
        "(yet). These rows are candidates for future versions.",
}
STATUS_LABEL = {"supported": "supported", "partial": "**partial**", "unsupported": "**unsupported**"}


def cell(text):
    """Markdown table cell: escape pipes, keep it on one line."""
    return text.replace("|", "\\|").replace("\n", " ")


def principle(why):
    return next(p for p in check.PRINCIPLES if why == p or why.startswith(p + ":"))


def why_text(why):
    """'detail (*principle*)', or just '*principle*'."""
    p = principle(why)
    detail = why[len(p) + 1:].strip()
    return f"{detail} (*{p}*)" if detail else f"*{p}*"


def sentence(text):
    return text[0].upper() + text[1:] if text and text[0].islower() else text


def link(s):
    return f"[{os.path.basename(s.rel)}](../tests/ts-features/{s.rel})"


def tool_versions():
    """tsc and Node.js versions used by CI (.github/workflows/ci.yml), compiler options of the samples."""
    ci = open(os.path.join(REPO, ".github", "workflows", "ci.yml"), encoding="utf-8").read()
    m = re.search(r"npm install -g typescript@([\w.]+)", ci)
    tsc = m.group(1) if m else "?"
    m = re.search(r'node-version:\s*"?(\d+)', ci)
    node = m.group(1) if m else "?"
    opts = json.load(open(os.path.join(REPO, "tests", "tsc", "tsconfig.json"), encoding="utf-8"))["compilerOptions"]
    return tsc, node, opts


def features(samples):
    """One row per feature: (feature, category, status, [samples]) in category order, then by file name."""
    rows = {}
    for s in samples:
        rows.setdefault(s.feature, []).append(s)
    out = []
    for feature, group in rows.items():
        group.sort(key=lambda s: (s.part != "ok", s.rel))  # .ok first
        out.append((feature, group[0].meta["category"], group[0].status, group))
    order = {c: i for i, c in enumerate(check.CATEGORIES)}
    status_order = {"supported": 0, "partial": 1, "unsupported": 2}
    out.sort(key=lambda r: (order[r[1]], status_order[r[2]], r[3][0].rel))
    return out


def notes(status, group):
    if status == "supported":
        return sentence(group[0].meta.get("note", ""))
    if status == "unsupported":
        s = group[0]
        text = "**Why:** " + why_text(s.meta["why"]) + ". **Instead:** " + s.meta["instead"]
        if s.meta.get("note"):
            text = sentence(s.meta["note"]) + ". " + text
        return text
    ok = next(s for s in group if s.part == "ok")
    no = next(s for s in group if s.part == "no")
    text = ""
    if ok.meta.get("note"):
        text += "**Works:** " + ok.meta["note"] + ". "
    text += "**Not supported:** " + why_text(no.meta["why"]) + ". **Instead:** " + no.meta["instead"]
    return text


def render(samples):
    rows = features(samples)
    tsc, node, opts = tool_versions()
    total = {st: sum(1 for r in rows if r[2] == st) for st in check.STATUSES}
    lines = []
    w = lines.append
    w("# TypeScript feature coverage")
    w("")
    w("<!-- Generated by scripts/gen-ts-features.py from the samples in tests/ts-features; do not edit. -->")
    w("")
    w("E7 is a strict subset of TypeScript: every E7 program passes `tsc --strict` and prints exactly what Node.js "
      "prints. There is no official definition of a \"strict TypeScript subset\", so this page defines E7's: one row "
      "per TypeScript feature, covering the chapters of the "
      "[TypeScript handbook](https://www.typescriptlang.org/docs/handbook/) and the JavaScript syntax and standard "
      "library that TypeScript programs use. The normative language definition is [SPEC.md](SPEC.md).")
    w("")
    w("## How features are selected")
    w("")
    w("A TypeScript feature is supported unless it conflicts with one of E7's principles. Every row that is not "
      "fully supported names the principle that excludes it:")
    w("")
    for i, p in enumerate(check.PRINCIPLES, 1):
        w(f"{i}. {PRINCIPLE_TEXT[p]}")
    w("")
    w("## How the table is verified")
    w("")
    lib = " + ".join(opts.get("lib", []))
    w(f"Every row is backed by a small program in [tests/ts-features](../tests/ts-features), whose header states the "
      f"feature, its status and, for features that are not supported, the reason, the E7 alternative and the "
      f"expected compile error. [check.py](../tests/ts-features/check.py) (the `ts_features` test of `ctest`, run "
      f"when Node.js is installed) enforces:")
    w("")
    w("- **supported**: the sample compiles with E7 and prints exactly what Node.js prints;")
    w("- **unsupported**: E7 rejects the sample with the stated compile error;")
    w("- **partial**: two samples, `<name>.ok.ts` (checked like a supported one, showing what works) and "
      "`<name>.no.ts` (checked like an unsupported one, showing what does not).")
    w("")
    w(f"All samples, including the rejected ones, are valid TypeScript: CI type-checks them with TypeScript {tsc} "
      f"(`tsc --strict`, target {opts.get('target')}, module {opts.get('module')}, lib {lib}, with "
      f"[lib/e7.d.ts](../lib/e7.d.ts); [tests/ts-features/tsconfig.json](../tests/ts-features/tsconfig.json)) "
      f"and compares the output of the supported ones with Node.js {node}. This page is generated from the "
      f"sample headers by `python3 scripts/gen-ts-features.py`, and CI fails when it is out of date.")
    w("")
    w("Out of scope: features that do not type-check under these options, such as CommonJS-only syntax "
      "(`export =`, `import x = require()`), `with` statements and other sloppy-mode JavaScript, and Node.js or "
      "browser APIs (`process`, `fetch`, the DOM). E7's own extensions (fixed-width integers, `e7:` modules, host "
      "functions) are described in SPEC.md.")
    w("")
    w("## Summary")
    w("")
    w(f"{len(rows)} features: **{total['supported']} supported**, **{total['partial']} partial**, "
      f"**{total['unsupported']} unsupported** ({len(samples)} sample programs).")
    w("")
    w("| Category | Supported | Partial | Unsupported |")
    w("|---|---:|---:|---:|")
    for cat in check.CATEGORIES:
        c = [r for r in rows if r[1] == cat]
        if not c:
            continue
        anchor = re.sub(r"[^a-z0-9 -]", "", cat.lower()).replace(" ", "-")
        n = {st: sum(1 for r in c if r[2] == st) for st in check.STATUSES}
        w(f"| [{cat}](#{anchor}) | {n['supported']} | {n['partial']} | {n['unsupported']} |")
    w(f"| **Total** | **{total['supported']}** | **{total['partial']}** | **{total['unsupported']}** |")
    w("")
    reasons = {p: 0 for p in check.PRINCIPLES}
    for s in samples:
        if s.expect == "unsupported":
            reasons[principle(s.meta["why"])] += 1
    w("Reasons for the unsupported features and the unsupported parts of partial ones: " +
      ", ".join(f"{p} {n}" for p, n in reasons.items()) + ".")
    w("")
    w("Within each category, supported features come first, then partial and unsupported ones.")
    w("")
    for cat in check.CATEGORIES:
        c = [r for r in rows if r[1] == cat]
        if not c:
            continue
        w(f"## {cat}")
        w("")
        w("| Feature | Status | Notes / what to write instead | Sample |")
        w("|---|---|---|---|")
        for feature, _, status, group in c:
            samples_cell = ", ".join(link(s) for s in group)
            w(f"| {cell(feature)} | {STATUS_LABEL[status]} | {cell(notes(status, group))} | {samples_cell} |")
        w("")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="exit with 1 if docs/TYPESCRIPT.md is not up to date")
    a = ap.parse_args()
    samples = check.load(FEATURES)
    problems = check.validate(samples)
    if problems:
        for rel, p in problems:
            print(f"{rel}: {p}", file=sys.stderr)
        return 1
    text = render(samples)
    if a.check:
        current = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if current != text:
            print("docs/TYPESCRIPT.md is out of date: run python3 scripts/gen-ts-features.py", file=sys.stderr)
            return 1
        print("docs/TYPESCRIPT.md is up to date")
        return 0
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {os.path.relpath(OUT, REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
