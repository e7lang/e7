#!/usr/bin/env python3
"""Tests for the command line developer experience:
  * `e7 test` (discovery, isolation, failures, budget / memory limits, --filter, --json, exit codes)
  * compiler diagnostics with source excerpts (e7 / e7c)

usage: cli_test.py --e7 <path/to/e7> --e7c <path/to/e7c>
"""
import argparse, json, os, re, subprocess, sys, tempfile

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
          "assertEqual failed: lists\n      expected: [ 1, 3 ]\n      actual:   [ 1, 2 ]\n        at testAssertEqualFails (cases.test.ts:8)" in out, out)
    check("failing: output of failing test shown", "    | before the failure" in out, out)
    check("failing: boolean false", "testReturnsFalse" in out and "test returned false" in out, out)
    check("failing: throw", "thrown on purpose\n        at testThrows (cases.test.ts:11)" in out, out)
    check("failing: runtime error", "index 2 out of range" in out, out)
    check("failing: budget", "instruction budget exhausted" in out, out)
    check("failing: memory", "out of memory (limit 8000000 bytes)" in out, out)
    check("failing: stack overflow collapsed", "... 19998 more frames of deep (recursion)" in out, out)
    check("failing: isolation (fresh VM per test)",
          re.search(r"✓ cases\.test\.ts › testIsolationA", out) and re.search(r"✓ cases\.test\.ts › testIsolationB", out), out)
    check("failing: parameters rejected", "test functions must not take parameters" in out, out)
    check("failing: return type rejected", "test functions must return void or boolean" in out, out)
    check("failing: module initialization error",
          "module initialization failed: index 7 out of range (length 3)\n        at <init> (init.test.ts:3)" in out, out)

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

    print("\n%d failed" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
