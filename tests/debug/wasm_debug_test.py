#!/usr/bin/env python3
"""Tests for the debugger JSON protocol of the WebAssembly entry points (tools/e7wasm.cpp), using
the same file built natively (E7W_NATIVE_MAIN). The commands map 1:1 to e7w_dbg_* calls.

usage: wasm_debug_test.py --e7wasm <path/to/e7wasm_native>
"""
import argparse, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
failures = []


def check(name, cond, detail=""):
    if cond:
        print("ok   " + name)
    else:
        print("FAIL " + name + ("\n" + str(detail) if detail else ""))
        failures.append(name)


def session(exe, *cmds):
    p = subprocess.run([exe, "prog.ts", "--dbg", "file lib/util.ts"] + list(cmds), cwd=HERE,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    lines = p.stdout.decode("utf-8").splitlines()
    out = [json.loads(l) for l in lines[1:]]
    assert len(out) == len(cmds), (cmds, lines, p.stderr)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--e7wasm", required=True)
    exe = os.path.abspath(ap.parse_args().e7wasm)

    r = session(exe, "bp prog.ts 12,999", "bp lib/util.ts 3", "launch entry", "bps", "resume continue", "stack",
                "scopes 0", "vars 1", "eval 0 a", "resume next", "resume stepOut", "resume continue",
                "resume continue", "resume continue", "scopes 0", "vars 1", "vars 3", "vars 3 1 1",
                "eval 0 rec.inner", "eval 0 nope", "resume stepIn", "resume continue", "stack",
                "resume continue", "resume continue")
    (bp1, bp2, entry, bps, hit, stack, scopes, loc, ev, nxt, out, hit2, hit3, dbg, scopes2, loc2, p, page, ev2, bad,
     step, util, stack2, done, after) = r
    check("set breakpoints before launch", [b["verified"] for b in bp1] == [False, False] and bp2[0]["id"] == 3, (bp1, bp2))
    check("entry event", entry["event"] == "stopped" and entry["reason"] == "entry" and
          entry["frame"] == {"name": "<init>", "file": "prog.ts", "line": 9, "column": 1}, entry)
    check("breakpoints verified after load", [(b["id"], b["line"], b["verified"]) for b in bps] ==
          [(3, 3, True), (1, 12, True), (2, 999, False)] or
          sorted((b["id"], b["line"], b["verified"]) for b in bps) == [(1, 12, True), (2, 999, False), (3, 3, True)], bps)
    check("breakpoint event", hit["reason"] == "breakpoint" and hit["breakpoints"] == [1] and hit["frame"]["line"] == 12, hit)
    check("stack", [(f["id"], f["name"], f["line"], f["column"]) for f in stack] == [(0, "add", 12, 3), (1, "<init>", 43, 1)], stack)
    check("scopes", [s["name"] for s in scopes] == ["Locals", "Globals"] and scopes[0]["count"] == 3, scopes)
    check("variables", [(v["name"], v["type"], v["value"]) for v in loc] == [("a", "number", "5"), ("b", "number", "6"), ("s", "number", "0")], loc)
    check("evaluate", ev == {"ok": True, "name": "a", "type": "number", "value": "5", "ref": 0, "count": 0}, ev)
    check("step over", nxt["reason"] == "step" and nxt["frame"]["line"] == 13, nxt)
    check("step out", out["reason"] == "step" and out["frame"]["name"] == "<init>", out)
    check("main: breakpoint twice, then debugger statement",
          hit2["frame"]["line"] == 12 and hit3["frame"]["line"] == 12 and dbg["reason"] == "debugger" and dbg["frame"]["line"] == 39,
          (hit2, hit3, dbg))
    check("main scopes", [s["name"] for s in scopes2] == ["Locals", "Globals"], scopes2)
    pv = [v for v in loc2 if v["name"] == "p"][0]
    check("object variable", pv["type"] == "Point" and pv["value"] == "Point { x: 3, y: 4 }" and pv["ref"] == 3 and pv["count"] == 2, loc2)
    check("expand object", [(v["name"], v["value"]) for v in p] == [("x", "3"), ("y", "4")], p)
    check("paging", [(v["name"], v["value"]) for v in page] == [("y", "4")], page)
    check("evaluate path", ev2["ok"] and ev2["type"] == "Object" and ev2["value"] == "{ v: 1 }" and ev2["ref"] > 0, ev2)
    check("evaluate error", bad == {"ok": False, "error": "no variable named 'nope'"}, bad)
    check("step in", step["reason"] == "step" and step["frame"]["line"] == 40, step)
    check("breakpoint in imported file", util["reason"] == "breakpoint" and util["frame"]["file"] == "lib/util.ts" and
          util["frame"]["line"] == 3, util)
    check("stack across files", [f["file"] for f in stack2] == ["lib/util.ts", "prog.ts"], stack2)
    check("terminated with result", done["event"] == "terminated" and done["status"] == 0 and
          done["result"] == {"type": "number", "value": 28}, done)
    check("resume after the end", after["event"] == "terminated" and after["status"] == 12, after)

    # runtime error: exception stop, then the error
    r = session(exe, "launch fail", "stack", "scopes 0", "vars 1", "resume continue")
    check("exception stop", r[0]["event"] == "stopped" and r[0]["reason"] == "exception" and
          r[0]["message"] == "index 5 out of range (length 1)" and r[0]["frame"]["line"] == 46, r[0])
    check("exception locals", [(v["name"], v["value"]) for v in r[3]] == [("xs", "[ 1 ]")], r[3])
    check("exception terminates", r[4]["event"] == "terminated" and r[4]["status"] == 2 and
          "at fail (prog.ts:46:12)" in r[4]["error"], r[4])

    # time slices: "running" events, pause between slices
    r = session(exe, "slice 1000", "launch spin", "pause", "resume continue", "stack", "terminate")
    check("slice: running", r[1]["event"] == "running", r[1])
    check("slice: pause between slices", r[3]["event"] == "stopped" and r[3]["reason"] == "pause" and
          r[4][0]["name"] == "spin", r[3:5])
    check("terminate", r[5]["event"] == "terminated", r[5])

    # output is delivered with events
    r = session(exe, "bp prog.ts 41", "launch log")
    check("program output in events", r[1]["event"] == "terminated" and r[1]["output"] == ["hello from prog"], r[1])

    print("\n%d failure(s)" % len(failures) if failures else "\nall wasm debugger tests passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
