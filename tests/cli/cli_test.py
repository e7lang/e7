#!/usr/bin/env python3
"""Tests for the command line developer experience:
  * `e7 test` (discovery, isolation, failures, budget / memory limits, --filter, --json, exit codes)
  * compiler diagnostics with source excerpts (e7 / e7c)
  * detached signatures (e7c --sign-file / --verify-file) and tools/manifest.py

usage: cli_test.py --e7 <path/to/e7> --e7c <path/to/e7c>
"""
import argparse, hashlib, json, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
failures = []


def run(cmd, cwd=None):
    env = dict(os.environ, TZ="UTC", NO_COLOR="1")
    p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300, env=env)
    return p.returncode, p.stdout.decode("utf-8", "replace").replace("\r\n", "\n"), p.stderr.decode("utf-8", "replace").replace("\r\n", "\n")


def check(name, cond, detail=""):
    if cond:
        print("ok   " + name)
    else:
        print("FAIL " + name + ("\n" + detail if detail else ""))
        failures.append(name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--e7", required=True)
    ap.add_argument("--e7c", required=True)
    a = ap.parse_args()
    e7 = os.path.abspath(a.e7)
    e7c = os.path.abspath(a.e7c)
    project = os.path.join(HERE, "project")
    failing = os.path.join(HERE, "failing")

    # ---- passing project, default discovery (skips node_modules / build)
    rc, out, err = run([e7, "test"], cwd=project)
    ok_lines = [l for l in out.splitlines() if l.startswith("✓ ")]
    check("project: exit code 0", rc == 0, out + err)
    check("project: 6 tests passed", "6 passed, 0 failed" in out, out)
    check("project: node_modules and build skipped", "skipped" not in out and "MustBeSkipped" not in out, out)
    check("project: source order", [l.split(" › ")[1].split(" (")[0] for l in ok_lines[:4]] ==
          ["testGcd", "testClamp", "testFloat", "testBoolean"], out)
    check("project: line format", re.search(r"^✓ math\.test\.ts › testGcd \(\d+\.\d ms\)$", out, re.M) is not None, out)
    check("project: subdirectory", "✓ sub/strings.test.ts › testSplit" in out, out)
    check("project: non-test exports and private functions not run", "helper" not in out and "testHidden" not in out, out)
    check("project: output of passing tests hidden", "clamp checked" not in out, out)
    rc, out, err = run([e7, "test", "--verbose"], cwd=project)
    check("project: --verbose shows output", "    | clamp checked" in out, out)

    # ---- explicit directory / file arguments, --filter
    rc, out, err = run([e7, "test", project])
    check("dir argument", rc == 0 and "6 passed" in out, out + err)
    rc, out, err = run([e7, "test", "--filter", "Gcd", os.path.join(project, "math.test.ts")])
    check("file argument + --filter", rc == 0 and "1 passed, 0 failed" in out and "testGcd" in out, out + err)
    rc, out, err = run([e7, "test", "--filter", "strings"], cwd=project)
    check("--filter matches file paths", rc == 0 and "2 passed, 0 failed" in out, out + err)
    rc, out, err = run([e7, "test", "--filter", "nothing-matches"], cwd=project)
    check("--filter without matches fails", rc == 1 and "no tests matched" in out, out + err)

    # ---- failures
    rc, out, err = run([e7, "test", "--budget", "1000000", "--mem", "8000000"], cwd=failing)
    check("failing: exit code 1", rc == 1, out + err)
    check("failing: summary", "4 passed, 11 failed" in out, out)
    check("failing: compile error with suggestion and excerpt",
          "✗ broken.test.ts (compile error)" in out and "did you mean 'total'?" in out and
          "     4 |   assertEqual(totl, 1);\n       |               ^" in out, out)
    check("failing: assertEqual details + stack trace",
          "assertEqual failed: lists\n      expected: [ 1, 3 ]\n      actual:   [ 1, 2 ]\n        at testAssertEqualFails (cases.test.ts:8:3)" in out, out)
    check("failing: output of failing test shown", "    | before the failure" in out, out)
    check("failing: boolean false", "testReturnsFalse" in out and "test returned false" in out, out)
    check("failing: throw", "thrown on purpose\n        at testThrows (cases.test.ts:11:38)" in out, out)
    check("failing: runtime error", "index 2 out of range" in out, out)
    check("failing: budget", "instruction budget exhausted" in out, out)
    check("failing: memory", "out of memory (limit 8000000 bytes)" in out, out)
    check("failing: stack overflow collapsed", "... 19998 more frames of deep (recursion)" in out, out)
    check("failing: isolation (fresh VM per test)",
          re.search(r"✓ cases\.test\.ts › testIsolationA", out) and re.search(r"✓ cases\.test\.ts › testIsolationB", out), out)
    check("failing: parameters rejected", "test functions must not take parameters" in out, out)
    check("failing: return type rejected", "test functions must return void or boolean" in out, out)
    check("failing: module initialization error",
          "module initialization failed: index 7 out of range (length 3)\n        at <init> (init.test.ts:3:18)" in out, out)

    # ---- --json
    rc, out, err = run([e7, "test", "--json", "--budget", "1000000", "--mem", "8000000"], cwd=failing)
    try:
        j = json.loads(out)
    except ValueError as ex:
        j = None
        check("json: parses", False, str(ex) + "\n" + out)
    if j is not None:
        check("json: exit code 1", rc == 1)
        check("json: counts", j["passed"] == 4 and j["failed"] == 11, out)
        byname = {t["name"]: t for t in j["tests"]}
        check("json: per-test results", byname["testPasses"]["ok"] is True and byname["testThrows"]["ok"] is False and
              "thrown on purpose" in byname["testThrows"]["error"] and byname["testPasses"]["error"] is None, out)
        check("json: captured output", byname["testAssertEqualFails"]["output"] == "before the failure\n", out)
        check("json: compile error", any(f["file"] == "broken.test.ts" and f["compileError"] and "totl" in f["compileError"]
                                         for f in j["files"]), out)
        check("json: durations", all(isinstance(t["durationMs"], (int, float)) for t in j["tests"]), out)

    # ---- budget flag: a test that needs more than the budget fails
    rc, out, err = run([e7, "test", "--budget", "1e5", "--filter", "InfiniteLoop"], cwd=failing)
    check("--budget (scientific notation)", rc == 1 and "0 passed, 2 failed" in out and "instruction budget exhausted" in out, out + err)

    # ---- no test files
    with tempfile.TemporaryDirectory() as empty:
        rc, out, err = run([e7, "test"], cwd=empty)
        check("no test files", rc == 1 and "no test files found" in err, out + err)
    rc, out, err = run([e7, "test", os.path.join(project, "math.ts"), "--filter", "zzz"])
    check("non-test file argument without tests", rc == 1, out + err)
    rc, out, err = run([e7, "test", os.path.join(HERE, "does-not-exist")])
    check("missing path", rc == 2 and "no such file" in err, out + err)
    rc, out, err = run([e7, "test", "--bogus"])
    check("usage on unknown option", rc == 2 and "usage: e7 test" in err, out + err)

    # ---- diagnostics with source excerpts
    broken = os.path.join(failing, "broken.test.ts")
    for tool in (e7, e7c):
        name = os.path.basename(tool)
        args = [tool, broken] + (["--check"] if tool == e7c else [])
        rc, out, err = run(args)
        first = err.splitlines()[0] if err else ""
        check(name + ": first line format unchanged",
              rc == 1 and re.match(r"^.*broken\.test\.ts:4:15: error: cannot find name 'totl'; did you mean 'total'\?$", first), err)
        check(name + ": excerpt with caret", " 4 |   assertEqual(totl, 1);\n   |               ^\n" in err, err)
        rc, out, err = run(args + ["--no-excerpt"])
        check(name + ": --no-excerpt", rc == 1 and "|" not in err and "did you mean" in err, err)

    # ---- detached signatures (e7c --sign-file / --verify-file) and tools/manifest.py
    manifest_py = os.path.join(HERE, "..", "..", "tools", "manifest.py")
    with tempfile.TemporaryDirectory() as d:
        p = lambda *n: os.path.join(d, *n)
        with open(p("seed"), "w") as f:  # seed 01..20: known-answer signature (tests/capi_test.cpp)
            f.write(bytes(range(1, 33)).hex() + "\n")
        with open(p("kat.json"), "w", newline="\n") as f:
            f.write('{"version":"2","killSwitch":false}')
        rc, out, err = run([e7c, "--sign-file", p("kat.json"), "--sign", p("seed")])
        sig = open(p("kat.json.sig")).read() if os.path.exists(p("kat.json.sig")) else ""
        check("--sign-file: known answer", rc == 0 and sig == "1549a30db54e6fc64e15efee8a9b5222a22be751830dd98dd5f97d30c1bc6b09"
              "2cdaa2efdc5a609a338b827ca32a0a9d1d9212ffd3eb51bfc9eb3b073487e607\n", out + err + sig)
        rc, out, err = run([e7c, "--keygen", p("release")])
        rc2, out2, err2 = run([e7c, "--keygen", p("other")])
        check("--keygen", rc == 0 and rc2 == 0, out + err + out2 + err2)
        rc, out, err = run([e7c, "--sign-file", p("kat.json")])
        check("--sign-file without --sign: usage error", rc == 2 and "--sign" in err, out + err)
        rc, out, err = run([e7c, "--verify-file", p("kat.json"), "--trust", p("other.pub")])
        check("--verify-file: untrusted key rejected", rc == 1 and "NOT valid" in err, out + err)
        with open(p("app.ts"), "w") as f:
            f.write("export function selfTest(): boolean { return true; }\nexport function answer(): number { return 42; }\n")
        rc, out, err = run([e7c, p("app.ts"), "--sign", p("release.seed"), "-o", p("app.e7b")])
        check("compile + sign module", rc == 0, out + err)
        rc, out, err = run([sys.executable, manifest_py, "--module", p("app.e7b"), "--version", "2026.11.1", "--url",
                            "app-2026.11.1.e7b", "--rollout", "10", "--serial", "7", "--sign", p("release.seed"),
                            "--e7c", e7c, "-o", p("manifest.json")])
        check("manifest.py: exit code", rc == 0, out + err)
        try:
            m = json.load(open(p("manifest.json")))
        except Exception as e:  # noqa: BLE001
            m = {"error": str(e)}
        want = {"version": "2026.11.1", "url": "app-2026.11.1.e7b", "rolloutPercent": 10, "killSwitch": False,
                "minVm": "0.1.0", "serial": 7, "sha256": hashlib.sha256(open(p("app.e7b"), "rb").read()).hexdigest()}
        check("manifest.py: fields", m == want, json.dumps(m))
        rc, out, err = run([e7c, "--verify-file", p("manifest.json"), "--trust", p("other.pub"), "--trust", p("release.pub")])
        check("--verify-file: manifest signature (second trusted key)", rc == 0 and "signature ok" in out, out + err)
        text = open(p("manifest.json")).read()
        with open(p("manifest.json"), "w", newline="\n") as f:
            f.write(text.replace('"killSwitch": false', '"killSwitch": true'))
        rc, out, err = run([e7c, "--verify-file", p("manifest.json"), "--trust", p("release.pub")])
        check("--verify-file: tampered manifest rejected", rc == 1, out + err)
        rc, out, err = run([sys.executable, manifest_py, "--kill-switch", "--serial", "8", "--sign", p("release.seed"),
                            "--e7c", e7c, "-o", p("kill.json")])
        k = json.load(open(p("kill.json"))) if rc == 0 else {}
        check("manifest.py --kill-switch", k == {"killSwitch": True, "serial": 8}, out + err)
        rc, out, err = run([e7c, "--verify-file", p("kill.json"), "--sig", p("kill.json.sig"), "--trust", p("release.pub")])
        check("kill switch manifest signed", rc == 0, out + err)
        rc, out, err = run([e7c, p("app.ts"), "-o", p("unsigned.e7b")])
        rc, out, err = run([sys.executable, manifest_py, "--module", p("unsigned.e7b"), "--version", "1", "--url", "u.e7b",
                            "-o", p("m2.json")])
        check("manifest.py: unsigned module refused", rc != 0 and "not signed" in err, out + err)
        # e7c --disasm (src/core/disasm.cpp; no other test reaches it)
        with open(p("dis.ts"), "w") as f:
            f.write("class P { constructor(public x: number) {} get(): number { return this.x; } }\n"
                    "const m = new Map<string, number>([[\"a\", 1]]);\n"
                    "let s = 0;\n"
                    "for (let i = 0; i < 3; i++) { try { s += new P(i).get() + m.get(\"a\")!; } catch (e) { s = -1; } }\n"
                    "const f = (k: number): string => \"k\" + k + s;\n"
                    "console.log(f(1), [1n, 2n].length, u32(7) >>> u32(1));\n")
        rc, out, err = run([e7c, p("dis.ts"), "--disasm", "--debug"])
        check("--disasm: exit code and header", rc == 0 and "module dis.ts (requires e7vm" in out, out + err)
        check("--disasm: natives, globals, classes, functions",
              all(x in out for x in ("native #", "global #", "class #0 P", "func #0 <init>", "CALLN", "CLOSURE")), out)
        check("--disasm: no output file without -o", not os.path.exists(p("dis.e7b")), "")

    # ---- e7c --info: the bytecode format and the `requires` header field; there is no --target
    with tempfile.TemporaryDirectory() as tmp:
        def p(name):
            return os.path.join(tmp, name)
        with open(p("private.ts"), "w") as f:
            f.write("class Counter { #n = 0; inc(): number { return ++this.#n; } }\n"
                    "export function run(): number { return new Counter().inc(); }\n")
        rc, out, err = run([e7c, p("private.ts"), "-o", p("private.e7b")])
        rc2, out2, err2 = run([e7c, "--info", p("private.e7b")])
        check("--info: format and requires", rc == 0 and rc2 == 0 and "format:   3\n" in out2 and
              "requires: e7vm 0.1.0" in out2, out + err + out2 + err2)
        rc, out, err = run([e7c, p("private.ts"), "--target", "0.1", "-o", p("t.e7b")])
        check("--target is not an option", rc == 2 and "usage" in err, out + err)

    print("\n%d failed" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
