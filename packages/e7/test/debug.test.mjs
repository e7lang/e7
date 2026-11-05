// Tests for the debugger API of @e7lang/e7 (createDebugSession), against the real WebAssembly build.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createDebugSession, E7Error } from "@e7lang/e7";

const dir = new URL("./fixtures/debug/", import.meta.url);
const readFile = (p) => {
  try {
    return readFileSync(new URL(p, dir), "utf8");
  } catch {
    return undefined;
  }
};
const session = (opts = {}) => createDebugSession({ entry: "main.ts", readFile, ...opts });
const vars = (s, ref) => Object.fromEntries(s.variables(ref).map((v) => [v.name, v]));
const locals = (s, frame = 0) => vars(s, s.scopes(frame).find((x) => x.name === "Locals").ref);

test("debugger statement, stack, scopes, variables, output", async () => {
  const s = await session();
  const output = [];
  s.on("output", (l) => output.push(l));
  assert.equal(s.state, "idle");
  const ev = await s.launch();
  assert.equal(ev.event, "stopped");
  assert.equal(ev.reason, "debugger");
  assert.equal(s.state, "stopped");
  assert.deepEqual(ev.frame, { name: "main", file: "main.ts", line: 29, column: 3 });
  assert.deepEqual(output, ["top-level start"]);
  assert.deepEqual(s.stack().map((f) => [f.id, f.name, f.line]), [[0, "main", 29]]);
  assert.deepEqual(s.scopes(0).map((x) => x.name), ["Locals", "Globals"]);
  const l = locals(s);
  assert.equal(l.r.value, "8");
  assert.equal(l.p.type, "Point");
  assert.deepEqual(s.variables(l.p.ref).map((v) => [v.name, v.value]), [["x", "3"], ["y", "4"]]);
  assert.equal(l.m.type, "Map");
  assert.deepEqual(s.variables(l.m.ref).map((v) => v.value), ["1"]);
  const inner = vars(s, vars(s, l.nested.ref).inner.ref);
  assert.equal(inner.list.type, "Array");
  assert.equal(inner.list.count, 3);
  assert.deepEqual(s.variables(inner.list.ref, 1, 2).map((v) => [v.name, v.value]), [["1", "2"], ["2", "3"]]);
  // closure: the captured variable of the counter
  assert.equal(l.counter.type, "function");
  assert.ok(l.counter.ref > 0);
  assert.deepEqual(s.variables(l.counter.ref).map((v) => [v.name, v.value]), [["n", "11"]]);
  const g = vars(s, s.scopes(0).find((x) => x.name === "Globals").ref);
  assert.equal(g.calls.value, "2");
  // evaluate
  assert.equal(s.evaluate(0, "nested.inner.list.length").value, "3");
  assert.equal(s.evaluate(0, "p.x").value, "3");
  assert.throws(() => s.evaluate(0, "nope"), E7Error);
  const done = await s.continue();
  assert.equal(done.event, "terminated");
  assert.equal(done.status, "ok");
  assert.equal(done.result, 28);
  assert.deepEqual(output, ["top-level start", "r = 8"]);
  assert.equal(s.state, "terminated");
  assert.deepEqual(s.stack(), []);
});

test("breakpoints in the entry and an imported file, stepping", async () => {
  const s = await session();
  const bp = s.setBreakpoints("main.ts", [7, 33]);
  assert.deepEqual(bp.map((b) => [b.requestedLine, b.verified]), [[7, false], [33, false]]);
  // an absolute path matches by suffix
  s.setBreakpoints("/somewhere/project/lib/geo.ts", [9]);
  let ev = await s.launch();
  assert.equal(ev.reason, "breakpoint");
  assert.equal(ev.frame.line, 7);
  assert.deepEqual(ev.breakpoints, [bp[0].id]);
  const all = s.breakpoints();
  assert.ok(all.find((b) => b.id === bp[0].id).verified);
  assert.deepEqual(s.stack().map((f) => [f.name, f.line]), [["add", 7], ["main", 27]]);
  assert.deepEqual(Object.keys(locals(s)), ["a", "b", "s"]);
  ev = await s.next();
  assert.equal(ev.reason, "step");
  assert.equal(ev.frame.line, 8);
  ev = await s.stepOut(); // back in main, after the call (the rest of line 27 is a register move)
  assert.equal(ev.frame.name, "main");
  assert.equal(ev.frame.line, 28);
  ev = await s.stepIn(); // into p.norm() (lib/geo.ts:9, also a breakpoint)
  assert.equal(ev.frame.file, "lib/geo.ts");
  assert.equal(ev.frame.name, "Point.norm");
  assert.equal(ev.frame.line, 9);
  assert.deepEqual(s.stack().map((f) => f.file), ["lib/geo.ts", "main.ts"]);
  s.setBreakpoints("main.ts", []);
  s.setBreakpoints("lib/geo.ts", []);
  ev = await s.continue();
  assert.equal(ev.reason, "debugger");
  ev = await s.continue();
  assert.equal(ev.event, "terminated");
});

test("stop on entry, call with arguments", async () => {
  const s = await session({ call: "sum", args: [4, "t="] });
  let ev = await s.launch({ stopOnEntry: true });
  assert.equal(ev.reason, "entry");
  assert.equal(ev.frame.name, "<init>");
  s.setBreakpoints("main.ts", [37]);
  ev = await s.continue();
  assert.equal(ev.reason, "breakpoint");
  assert.equal(ev.frame.name, "sum");
  assert.equal(locals(s).label.value, "'t='");
  s.setBreakpoints("main.ts", []);
  ev = await s.continue();
  assert.equal(ev.result, "t=6");
});

test("runtime error stops with the message, then terminates", async () => {
  const s = await session({ call: "fail" });
  const ev = await s.launch();
  assert.equal(ev.reason, "exception");
  assert.match(ev.message, /out of range/);
  assert.equal(ev.frame.line, 44);
  const done = await s.continue();
  assert.equal(done.status, "runtime_error");
  assert.match(done.error, /at fail \(main\.ts:44/);

  const t = await session({ call: "fail", breakOnError: false });
  assert.equal((await t.launch()).status, "runtime_error");
});

test("pause an infinite loop (in a call and in top-level code), terminate", async () => {
  const s = await session({ call: "spin", slice: 10_000 });
  let running = s.launch();
  let ev;
  // a slow machine may still be in the top-level code after the first wait: continue and pause again
  for (let attempt = 0; attempt < 20; attempt++) {
    await new Promise((r) => setTimeout(r, 50));
    assert.equal(s.state, "running");
    s.pause();
    ev = await running;
    assert.equal(ev.reason, "pause");
    if (ev.frame.name === "spin") break;
    running = s.continue();
  }
  assert.equal(ev.frame.name, "spin");
  assert.ok(Number(locals(s).i.value) > 0);
  const again = s.continue();
  await new Promise((r) => setTimeout(r, 20));
  const t = s.terminate();
  assert.equal(t.status, "aborted");
  assert.equal(await again, t);
  assert.equal(s.state, "terminated");

  const top = await createDebugSession({ entry: "loop.ts", readFile, slice: 10_000 });
  const out = [];
  top.on("output", (l) => out.push(l));
  const p = top.launch();
  await new Promise((r) => setTimeout(r, 50));
  top.pause();
  const pe = await p;
  assert.equal(pe.reason, "pause");
  assert.equal(pe.frame.name, "<init>");
  assert.deepEqual(out, ["looping"]);
  top.terminate();
});

test("instruction budget", async () => {
  const s = await session({ call: "spin", slice: 10_000, budget: 100_000 });
  const ev = await s.launch();
  assert.equal(ev.event, "terminated");
  assert.equal(ev.status, "budget_exceeded");
});

test("compile errors throw E7Error with diagnostics; sessions are independent", async () => {
  await assert.rejects(
    createDebugSession({ entry: "bad.ts", source: "let x: number = 'a';" }),
    (e) => e instanceof E7Error && e.diagnostics.length === 1 && e.diagnostics[0].line === 1,
  );
  const a = await createDebugSession({ entry: "a.ts", source: "export function main(): number { debugger; return 1; }" });
  const b = await createDebugSession({
    entry: "b.ts",
    files: { "b.ts": 'import { two } from "./two.ts";\nexport function main(): number { debugger; return two(); }', "two.ts": "export function two(): number { return 2; }" },
  });
  assert.equal((await a.launch()).frame.file, "a.ts");
  assert.equal((await b.launch()).frame.file, "b.ts");
  assert.equal((await b.continue()).result, 2);
  assert.equal((await a.continue()).result, 1);
});
