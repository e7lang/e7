// Tests for the @e7lang/e7 API. They import the package by name, so they run unchanged against the
// source tree (package self-reference) and against the installed tarball (CI: js.yml).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import * as e7mod from "@e7lang/e7";
import e7default, { init, compile, check, run, disassemble, listExports, inspect, formatDiagnostics, version, E7Error } from "@e7lang/e7";

const fixture = (f) => readFileSync(new URL(`./fixtures/${f}`, import.meta.url), "utf8");
const SIGN = fixture("sign.ts");
const TYPES = fixture("types.ts");
const HMAC = "0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6";
const pkg = JSON.parse(readFileSync(new URL("./package.json", import.meta.resolve("@e7lang/e7")), "utf8"));

test("version and init", async () => {
  assert.equal(version, pkg.version);
  const e7 = await init();
  assert.equal(await init(), e7, "init() is idempotent");
  assert.match(e7.version, /^\d+\.\d+\.\d+/);
  assert.equal(typeof e7default.run, "function");
  for (const k of ["init", "compile", "check", "run", "disassemble", "listExports", "inspect", "formatDiagnostics", "E7Error"]) assert.ok(k in e7mod, k);
});

test("lazy init: module functions work without init()", async () => {
  const r = await run("export function main(): number { return 6 * 7; }");
  assert.equal(r.status, "ok");
  assert.equal(r.result, 42);
});

test("compile: ok", async () => {
  const r = await compile(SIGN, { fileName: "sign.ts" });
  assert.equal(r.ok, true);
  assert.ok(r.bytecode instanceof Uint8Array);
  assert.ok(r.bytecode.length > 100);
  assert.deepEqual(r.diagnostics, []);
});

test("compile: type error diagnostics with file, line, column", async () => {
  const src = "// line 1\nconst ok: number = 1;\nlet x: number = 'a';\n";
  const r = await compile(src, { fileName: "bad.ts" });
  assert.equal(r.ok, false);
  assert.equal(r.bytecode, undefined);
  assert.equal(r.diagnostics.length, 1);
  const d = r.diagnostics[0];
  assert.equal(d.file, "bad.ts");
  assert.equal(d.line, 3);
  assert.equal(d.column, "let x: number = 'a';".indexOf("'a'") + 1);
  assert.equal(d.severity, "error");
  assert.match(d.message, /not assignable/);
  assert.equal(formatDiagnostics(r.diagnostics), `bad.ts:3:17: error: ${d.message}`);
});

test("compile: columns are UTF-16 based after non-ASCII text", async () => {
  const cases = [
    "/* é 👋 */ let x: number = 'a';",
    'const greeting: string = "héllo 世界 👋"; let x: number = \'a\';',
    "const t: string = `é${\"世\"}👋 ${1}`; let x: number = 'a';",
    "// 世界\nconst m: string = `é\n世👋`; let x: number = 'a';",
  ];
  for (const src of cases) {
    const r = await compile(src);
    const d = r.diagnostics[0];
    assert.equal(d.file, "main.ts");
    const lines = src.split("\n");
    assert.equal(d.line, lines.length, src);
    assert.equal(d.column, lines[lines.length - 1].indexOf("'a'") + 1, src);
  }
});

test("compile: syntax errors and several errors", async () => {
  const r = await compile("let x: number = ;\n");
  assert.equal(r.ok, false);
  assert.ok(r.diagnostics.length >= 1);
  assert.equal(r.diagnostics[0].line, 1);
  const r2 = await compile("let a: number = 'x';\nlet b: string = 1;\n");
  assert.equal(r2.ok, false);
  assert.ok(r2.diagnostics.length >= 1);
});

test("compile: rejects non-strings", async () => {
  await assert.rejects(compile(42), TypeError);
});

test("check", async () => {
  assert.deepEqual(await check(SIGN), []);
  const d = await check("let s: string = 5;", { fileName: "x.ts" });
  assert.equal(d.length, 1);
  assert.equal(d[0].file, "x.ts");
  // `any` does not exist in E7
  assert.ok((await check("let v: any = 1;")).length > 0);
});

test("imports via files map and readFile", async () => {
  const main = 'import { twice } from "./lib/util";\nexport function main(): number { return twice(21); }';
  const util = "export function twice(n: number): number { return n * 2; }";
  let r = await run(main, { files: { "./lib/util.ts": util } });
  assert.equal(r.result, 42, r.error);
  const read = [];
  r = await run(main, { fileName: "src/main.ts", readFile: (p) => (read.push(p), p === "src/lib/util.ts" ? util : undefined) });
  assert.equal(r.result, 42, r.error);
  assert.ok(read.includes("src/lib/util.ts"));
  r = await compile(main);
  assert.equal(r.ok, false);
  assert.match(r.diagnostics[0].message, /cannot find module/);
  r = await compile(main, { files: { "lib/util.ts": "export function twice(n: number): number { return 'x'; }" } });
  assert.equal(r.diagnostics[0].file, "lib/util.ts");
});

test("run: console output, Unicode, main() result", async () => {
  const r = await run('console.log("héllo, " + `${1 + 2}`);\nconsole.log("世界 👋");\nexport function main(): number { return 0.1 + 0.2; }');
  assert.equal(r.ok, true);
  assert.equal(r.status, "ok");
  assert.equal(r.code, 0);
  assert.deepEqual(r.output, ["héllo, 3", "世界 👋"]);
  assert.equal(r.result, 0.30000000000000004);
  assert.equal(r.error, null);
});

test("run: multi-line console.log stays one entry; no main", async () => {
  const r = await run('console.log("a\\nb"); console.log("tab\\there");');
  assert.deepEqual(r.output, ["a\nb", "tab\there"]);
  assert.equal(r.result, undefined);
  assert.equal(r.status, "ok");
});

test("run: sign.ts HMAC (main and call with args)", async () => {
  let r = await run(SIGN);
  assert.equal(r.result, HMAC, r.error);
  r = await run(SIGN, { call: "sign", args: ["amount=100&currency=CNY", "s3cr3t"] });
  assert.equal(r.result, HMAC, r.error);
});

test("compile -> run bytecode round trip", async () => {
  const { bytecode } = await compile(SIGN, { fileName: "sign.ts" });
  const copy = new Uint8Array(bytecode); // e.g. after writing / reading a .e7b file
  const r = await run(copy, { call: "sign", args: ["amount=100&currency=CNY", "s3cr3t"] });
  assert.equal(r.result, HMAC, r.error);
  assert.equal((await run(copy.buffer)).result, HMAC, "ArrayBuffer input");
  assert.equal((await run(bytecode)).result, HMAC);
});

test("run: argument and result types", async () => {
  const { bytecode } = await compile(TYPES);
  const call = async (name, ...args) => {
    const r = await run(bytecode, { call: name, args });
    assert.equal(r.status, "ok", r.error);
    assert.deepEqual(r.output, ["loaded"]);
    return r.result;
  };
  assert.equal(await call("add", 2, 40), 42);
  assert.equal(await call("add", 2147483647, 1), -2147483648);
  assert.equal(await call("twice", 9007199254740993n), 18014398509481986n);
  assert.equal(await call("twice", 5), 10n);
  assert.equal(await call("neg", -9223372036854775807n), 9223372036854775807n);
  assert.deepEqual(await call("rev", new Uint8Array([1, 2, 3])), new Uint8Array([3, 2, 1]));
  assert.deepEqual(await call("rev", new Uint8Array()), new Uint8Array());
  assert.equal(await call("not", false), true);
  assert.equal(await call("greet", "世界"), "héllo, 世界 👋");
  assert.ok(Number.isNaN(await call("nan")));
  await assert.rejects(run(bytecode, { call: "add", args: [1.5, 2] }), TypeError);
  await assert.rejects(run(bytecode, { call: "greet", args: [{}] }), TypeError);
  await assert.rejects(run(bytecode, { args: [1] }), TypeError);
});

test("run: errors", async () => {
  const { bytecode } = await compile(TYPES);
  let r = await run(bytecode, { call: "boom" });
  assert.equal(r.status, "runtime_error");
  assert.equal(r.code, 2);
  assert.equal(r.ok, false);
  assert.match(r.error, /boom/);
  assert.equal(r.result, undefined);
  assert.deepEqual(r.output, ["loaded"]);

  r = await run(bytecode, { call: "nope" });
  assert.equal(r.status, "not_found");
  r = await run(bytecode, { call: "greet", args: [] });
  assert.equal(r.status, "bad_arguments");
  r = await run(bytecode, { call: "greet", args: [1] });
  assert.equal(r.status, "bad_arguments");
  r = await run(bytecode, { call: "deep", args: [1e7] });
  assert.equal(r.status, "stack_overflow", r.error);

  r = await run("let x: number = 'a';", { fileName: "c.ts" });
  assert.equal(r.status, "compile_error");
  assert.equal(r.code, 11);
  assert.match(r.error, /^c\.ts:1:17: error: /);
  assert.equal(r.diagnostics.length, 1);

  r = await run(new Uint8Array([1, 2, 3, 4, 5]));
  assert.equal(r.status, "load_error");
  r = await run(new Uint8Array());
  assert.equal(r.status, "load_error");
  await assert.rejects(run(42), TypeError);
});

test("run: instruction budget", async () => {
  const { bytecode } = await compile(TYPES);
  const r = await run(bytecode, { call: "spin", budget: 1e6 });
  assert.equal(r.status, "budget_exceeded");
  assert.match(r.error, /budget/);
  // a budget large enough for the work succeeds; 0 = unlimited
  assert.equal((await run(SIGN, { budget: 1e7 })).result, HMAC);
  assert.equal((await run(SIGN, { budget: 0 })).result, HMAC);
  assert.equal((await run("let n = 0; for (let i = 0; i < 1000; i++) n += i;", { budget: 100 })).status, "budget_exceeded");
});

test("run: memory limit", async () => {
  const { bytecode } = await compile(TYPES);
  const r = await run(bytecode, { call: "hog", memoryLimit: 8 * 1024 * 1024, budget: 0 });
  assert.equal(r.status, "out_of_memory");
  assert.match(r.error, /out of memory/);
  // the instance stays usable afterwards
  assert.equal((await run(SIGN)).result, HMAC);
});

test("run: seed makes Math.random deterministic", async () => {
  const { bytecode } = await compile(TYPES);
  const a = await run(bytecode, { call: "rnd", seed: 7 });
  const b = await run(bytecode, { call: "rnd", seed: 7 });
  const c = await run(bytecode, { call: "rnd", seed: 8 });
  assert.equal(a.result, b.result);
  assert.notEqual(a.result, c.result);
  assert.ok(a.result >= 0 && a.result < 1);
});

test("disassemble", async () => {
  const src = "export function main(): number { return 1; }";
  const text = await disassemble(src);
  assert.match(text, /func #\d+ main/);
  const { bytecode } = await compile(src);
  assert.equal(await disassemble(bytecode), text);
  await assert.rejects(disassemble("let x: number = 'a';"), (e) => e instanceof E7Error && e.diagnostics.length === 1);
  await assert.rejects(disassemble(new Uint8Array([9, 9, 9])), E7Error);
});

test("listExports", async () => {
  const ex = await listExports(SIGN);
  assert.deepEqual(ex, [
    { name: "main", params: [], returns: "string" },
    { name: "sign", params: ["string", "string"], returns: "string" },
  ]);
  const t = await listExports(TYPES);
  assert.deepEqual(t.find((e) => e.name === "add"), { name: "add", params: ["i32", "i32"], returns: "i32" });
});

test("inspect", async () => {
  const e7 = await init();
  const { bytecode } = await compile(SIGN);
  const info = await inspect(bytecode);
  assert.equal(info.ok, true);
  assert.equal(info.status, "ok");
  assert.equal(info.format, 3);
  assert.equal(info.requiresVersion, "0.1.0");
  assert.equal(info.requiresVersionNumber, 0x000100);
  assert.equal(info.signed, false);
  assert.equal(info.keyId, null);
  assert.equal(info.debugInfo, false);
  assert.equal(e7.inspect((await compile(SIGN, { debug: true })).bytecode).debugInfo, true);
  // a module that needs a newer VM: patch the `requires` header field (offset 12) and the checksum
  const b = bytecode.slice();
  new DataView(b.buffer).setUint32(12, 0x630000, true);
  new DataView(b.buffer).setUint32(b.length - 4, crc32(b.subarray(0, b.length - 8)), true);
  const newer = await inspect(b);
  assert.equal(newer.ok, false);
  assert.equal(newer.status, "version_error");
  assert.equal(newer.requiresVersion, "99.0.0");
  const r = await run(b);
  assert.equal(r.status, "version_error");
  assert.equal(r.code, 14);
  assert.equal((await inspect(new Uint8Array([1, 2, 3]))).status, "load_error");
  await assert.rejects(inspect("source"), TypeError);
});

function crc32(p) {
  let c = 0xffffffff;
  for (const x of p) {
    c ^= x;
    for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
  }
  return ~c >>> 0;
}

test("synchronous instance API", async () => {
  const e7 = await init();
  assert.equal(e7.run(SIGN).result, HMAC);
  assert.deepEqual(e7.check(SIGN), []);
  assert.equal(e7.compile(SIGN).ok, true);
  assert.match(e7.disassemble(SIGN), /sign/);
});

test("large source and output", async () => {
  const lines = [];
  for (let i = 0; i < 3000; i++) lines.push(`export function f${i}(): number { return ${i}; }`);
  lines.push('export function main(): number { for (let i = 0; i < 20000; i++) console.log("line " + i); return f2999(); }');
  const r = await run(lines.join("\n"));
  assert.equal(r.status, "ok", r.error);
  assert.equal(r.result, 2999);
  assert.equal(r.output.length, 20000);
  assert.equal(r.output[19999], "line 19999");
});

// async / await: async exports and top-level await finish within run(); e7:test's later() /
// laterReject() are host-like operations the VM settles itself (this package has no async host functions)
const ASYNC = `import { later, laterReject } from "e7:test";
console.log("init");
const base = await later(40);
console.log("base " + base);
export async function main(): Promise<number> { return base + (await later(2)); }
export async function both(a: string, b: string): Promise<string> {
  const order: string[] = [];
  const pa = later(a).then((x) => { order.push(x); return x; });
  const pb = later(b).then((x) => { order.push(x); return x; });
  const all = await Promise.all([pa, pb]);
  return all.join("") + ":" + order.join("");
}
export async function caught(): Promise<string> {
  try {
    await laterReject(new Error("no such key"));
    return "not reached";
  } catch (e) {
    return e instanceof Error ? "caught " + e.message : "?";
  }
}
export async function uncaught(): Promise<number> { await laterReject(new Error("gone")); return 1; }
export async function forever(): Promise<number> { await new Promise<number>(() => {}); return 1; }
export async function big(x: u64): Promise<u64> { return (await later(x)) * 2; }
`;

test("run: async exports and top-level await", async () => {
  const { bytecode, diagnostics } = await compile(ASYNC, { fileName: "async.ts" });
  assert.deepEqual(diagnostics, []);
  let r = await run(bytecode);
  assert.equal(r.status, "ok", r.error);
  assert.equal(r.result, 42, "main(): Promise<number> gives the promise's value");
  assert.deepEqual(r.output, ["init", "base 40"], "top-level await finished before main()");
  r = await run(bytecode, { call: "both", args: ["a", "b"] });
  assert.equal(r.result, "ab:ab");
  r = await run(bytecode, { call: "caught" });
  assert.equal(r.result, "caught no such key");
  r = await run(bytecode, { call: "uncaught" });
  assert.equal(r.status, "runtime_error");
  assert.equal(r.code, 2);
  assert.match(r.error, /gone/);
  r = await run(bytecode, { call: "forever" });
  assert.equal(r.status, "runtime_error");
  assert.match(r.error, /never finishes/);
  r = await run(bytecode, { call: "big", args: [2n ** 63n + 1n] });
  assert.equal(r.result, 2n, "u64 wraps; i64 / u64 promise values are bigint");
  assert.deepEqual((await listExports(bytecode)).find((e) => e.name === "main"), { name: "main", params: [], returns: "number" });
});
