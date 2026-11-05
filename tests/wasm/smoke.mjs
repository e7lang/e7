// Smoke test for the WebAssembly build:  node tests/wasm/smoke.mjs <build-wasm>/e7wasm.js
import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import assert from "node:assert/strict";

const jsPath = process.argv[2];
const { default: createE7 } = await import(pathToFileURL(jsPath).href);
const m = await createE7();
const run = m.cwrap("e7w_run", "number", ["string", "number", "number", "number"]);
const str = (fn) => m.UTF8ToString(m.ccall(fn, "number", [], []));
const disasm = m.cwrap("e7w_disasm", "string", ["string"]);

function exec(src, budget = 1e8) {
  const status = run(src, budget, 64 * 1024 * 1024, 1);
  return { status, output: str("e7w_output"), result: str("e7w_result"), error: str("e7w_error") };
}

let r = exec(readFileSync(new URL("../../examples/embed/sign.ts", import.meta.url), "utf8"));
assert.equal(r.status, 0, r.error);
assert.equal(r.result, '"0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6"');

r = exec('console.log("héllo, " + `${1 + 2}`);\nexport function main(): number { return 0.1 + 0.2; }');
assert.equal(r.status, 0, r.error);
assert.equal(r.output, "héllo, 3\n");
assert.equal(r.result, "0.30000000000000004");

r = exec("let x: number = 'a';");
assert.equal(r.status, 11);
assert.match(r.error, /playground\.ts:1:\d+: error/);

r = exec("export function main(): void { while (true) {} }", 1e6);
assert.equal(r.status, 3);
assert.match(r.error, /budget/);

r = exec('export function main(): void { throw new Error("boom"); }');
assert.equal(r.status, 2);
assert.match(r.error, /boom/);

r = exec("function f(n: number): number { return n <= 0 ? 0 : 1 + f(n - 1); }\nexport function main(): number { return f(1e7); }");
assert.equal(r.status, 5, r.error);  // call depth limit, not a wasm stack overflow

assert.match(disasm("export function main(): number { return 1; }"), /func #\d+ main/);
// debugger JSON protocol (docs/DEBUGGING.md §7)
{
  const src = "function add(a: number, b: number): number {\n  const s = a + b;\n  return s;\n}\n" +
    "export function main(): number {\n  debugger;\n  console.log('hi');\n  return add(1, 2);\n}\n";
  const nbytes = new TextEncoder().encode(src).length;
  assert.equal(m.ccall("e7w_compile_ex", "number", ["string", "number", "string", "number"], [src, nbytes, "dbg.ts", 1]), 0);
  const ptr = m.ccall("e7w_bytes_ptr", "number", [], []), len = m.ccall("e7w_bytes_len", "number", [], []);
  const bc = m.HEAPU8.slice(ptr, ptr + len);
  const bp = m._malloc(len);
  m.HEAPU8.set(bc, bp);
  const J = (name, types = [], args = []) => JSON.parse(m.ccall(name, "string", types, args));
  m.ccall("e7w_dbg_create", null, ["number", "number", "number"], [64 * 1024 * 1024, 1, 0]);
  assert.deepEqual(J("e7w_dbg_set_breakpoints", ["string", "string"], ["dbg.ts", "2"]).map((b) => b.id), [1]);
  let ev = J("e7w_dbg_launch", ["number", "number", "string", "number"], [bp, len, "", 0]);
  assert.equal(ev.event, "stopped");
  assert.equal(ev.reason, "debugger");
  assert.deepEqual(ev.frame, { name: "main", file: "dbg.ts", line: 6, column: 3 });
  ev = J("e7w_dbg_resume", ["string"], ["continue"]);
  assert.equal(ev.reason, "breakpoint");
  assert.deepEqual(ev.output, ["hi"]);
  assert.deepEqual(J("e7w_dbg_stack").map((f) => [f.name, f.line]), [["add", 2], ["main", 8]]);
  const scopes = J("e7w_dbg_scopes", ["number"], [0]);
  assert.deepEqual(J("e7w_dbg_variables", ["number", "number", "number"], [scopes[0].ref, 0, 0]).map((v) => [v.name, v.value]),
    [["a", "1"], ["b", "2"], ["s", "0"]]);
  assert.equal(J("e7w_dbg_evaluate", ["number", "string"], [0, "b"]).value, "2");
  ev = J("e7w_dbg_resume", ["string"], ["stepOut"]);
  assert.equal(ev.reason, "step");
  assert.equal(ev.frame.name, "main");
  ev = J("e7w_dbg_resume", ["string"], ["continue"]);
  assert.equal(ev.event, "terminated");
  assert.deepEqual(ev.result, { type: "number", value: 3 });
  J("e7w_dbg_terminate");
  m._free(bp);
}

console.log("wasm smoke test OK, e7", str("e7w_version"));
