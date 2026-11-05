#!/usr/bin/env python3
"""Tests for the command-line debugger (`e7 debug`), driven through stdin.

usage: cli_debug_test.py --e7 <path/to/e7> [--e7c <path/to/e7c>]
"""
import argparse, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
failures = []


def run(cmd, stdin="", cwd=None):
    env = dict(os.environ, TZ="UTC", NO_COLOR="1")
    p = subprocess.run(cmd, cwd=cwd, input=stdin.encode(), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=120, env=env)
    return p.returncode, p.stdout.decode("utf-8", "replace").replace("\r\n", "\n"), \
        p.stderr.decode("utf-8", "replace").replace("\r\n", "\n")


def check(name, cond, detail=""):
    if cond:
        print("ok   " + name)
    else:
        print("FAIL " + name + ("\n" + detail if detail else ""))
        failures.append(name)


def contains_in_order(text, parts):
    pos = 0
    for p in parts:
        i = text.find(p, pos)
        if i < 0:
            return False
        pos = i + len(p)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--e7", required=True)
    ap.add_argument("--e7c")
    a = ap.parse_args()
    e7 = os.path.abspath(a.e7)
    prog = "prog.ts"

    # ---- stop at entry, breakpoints, stepping, frames, variables
    cmds = "\n".join([
        "b 12", "c", "locals", "bt", "n", "", "n",       # empty line repeats `n`
        "c", "c", "c",                                    # main: add twice, then `debugger;`
        "locals", "p p", "p arr[0][1]", "p rec.inner.v", "p m['k1']", "p names.length", "p tup[1]", "p nope",
        "globals", "b lib/util.ts:3", "delete 1", "c",
        "bt", "up", "p flag", "down", "list", "finish", "n", "c", ""])
    rc, out, err = run([e7, "debug", prog], cmds, cwd=HERE)
    check("entry stop", out.startswith("Stopped at entry, <init> (prog.ts:9:1)\n      9 | let total = 0;\n(e7db) "), out)
    check("breakpoint set", "Breakpoint 1 at prog.ts:12\n" in out, out)
    check("breakpoint hit in top-level call", "Breakpoint 1, add (prog.ts:12:3)\n     12 |   const s = a + b;" in out, out)
    check("locals", "a = 5\nb = 6\ns = 0\n" in out, out)
    check("backtrace", "*#0 add (prog.ts:12:3)\n #1 <init> (prog.ts:43:1)\n" in out, out)
    check("next + repeat", contains_in_order(out, ["add (prog.ts:13:3)", "add (prog.ts:14:3)", "<init> (prog.ts:43:1)\n     43 | add(5, 6);"]), out)
    check("debugger statement", "debugger statement, main (prog.ts:39:3)" in out, out)
    check("locals of main", contains_in_order(out, ["p = Point { x: 3, y: 4 }", "flag = true", "big = 12345678901234567890n",
                                                     "m = Map(1) { 'k1' => 1 }", "st = Set(2) { 7, 8 }",
                                                     "tup = [ 1, 't' ]", "nothing = null", "r = 8"]), out)
    check("print paths", contains_in_order(out, ["p = Point { x: 3, y: 4 }", "arr[0][1] = 2", "rec.inner.v = 1",
                                                  "m['k1'] = 1", "names.length = 2", "tup[1] = 't'",
                                                  "error: no variable named 'nope'"]), out)
    check("globals", "total = 22\nnames = [ 'a', 'b' ]\n" in out, out)
    check("breakpoint in imported file", "Breakpoint 2 at lib/util.ts:3\n" in out and
          "Breakpoint 2, scale (lib/util.ts:3:3)\n      3 |   return x * k;" in out, out)
    check("delete", "Deleted breakpoint 1\n" in out, out)
    check("frames up/down", contains_in_order(out, ["*#0 scale (lib/util.ts:3:3)\n #1 main (prog.ts:40:11)",
                                                     "#1 main (prog.ts:40:11)", "flag = true", "#0 scale (lib/util.ts:3:3)"]), out)
    check("list", "=>    3 |   return x * k;\n" in out, out)
    check("finish returns to the caller mid-statement", contains_in_order(out, ["main (prog.ts:40:7)", "main (prog.ts:41:3)"]), out)
    check("program result and exit", out.rstrip().endswith("[main returned 28]\n[program exited]") and rc == 0, out + err)

    # ---- --break, step into closure, finish, runtime error stop
    cmds = "\n".join(["s", "s", "p n", "finish", "c"])
    rc, out, err = run([e7, "debug", "--break", "prog.ts:36", "--break", "nowhere.ts:1", prog], cmds, cwd=HERE)
    check("--break", out.startswith("Breakpoint 1 at prog.ts:36 (pending)\nBreakpoint 2 at nowhere.ts:1 (pending)\n"), out)
    check("step into closure", contains_in_order(out, ["Breakpoint 1, main (prog.ts:36:3)", "makeCounter.<anonymous> (prog.ts:19:5)",
                                                        "makeCounter.<anonymous> (prog.ts:20:5)", "n = 11"]), out)

    rc, out, err = run([e7, "debug", "--call", "fail", "--break", "prog.ts:45", prog], "c\nbt\np xs\nc\n", cwd=HERE)
    check("runtime error stop", contains_in_order(out, ["Runtime error: index 5 out of range (length 1)\n  at fail (prog.ts:46:12)",
                                                         "*#0 fail (prog.ts:46:12)", "xs = [ 1 ]"]), out)
    check("runtime error exit", rc == 3 and "index 5 out of range (length 1)\n    at fail (prog.ts:46:12)" in err, out + err)

    # ---- quit, EOF, help, unknown command
    rc, out, err = run([e7, "debug", prog], "help\nfoo\nq\n", cwd=HERE)
    check("help", "b [file:]line" in out, out)
    check("unknown command", "unknown command 'foo'" in out, out)
    check("quit", rc == 0 and out.rstrip().endswith("[quit]"), out + err)
    rc, out, err = run([e7, "debug", prog], "", cwd=HERE)
    check("EOF runs to the end", rc == 0 and out.rstrip().endswith("[program exited]"), out + err)

    # ---- a bytecode file compiled with --debug
    if a.e7c:
        with tempfile.TemporaryDirectory() as tmp:
            out_file = os.path.join(tmp, "prog.e7b")
            rc, _, err = run([os.path.abspath(a.e7c), "--debug", prog, "-o", out_file], cwd=HERE)
            check("e7c --debug", rc == 0, err)
            rc, out, err = run([e7, "debug", "--break", "prog.ts:13", out_file], "locals\nc\n", cwd=HERE)
            check("debugging a .e7b", "Breakpoint 1, add (prog.ts:13:3)" in out and "s = 11" in out, out + err)
            # release bytecode: no columns in traces, still loads
            rc, _, err = run([os.path.abspath(a.e7c), prog, "-o", out_file], cwd=HERE)
            rc, out, err = run([e7, "--call", "fail", out_file], cwd=HERE)
            check("release traces without columns", rc == 3 and "at fail (prog.ts:46)" in err, err)

    print("\n%d failure(s)" % len(failures) if failures else "\nall cli debugger tests passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
