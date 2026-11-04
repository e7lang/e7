// Tests for the e7-js command-line tool (bin/e7-js.js of the installed package).
import { test } from "node:test";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, writeFileSync, readFileSync, existsSync, copyFileSync, mkdirSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const bin = fileURLToPath(new URL("./bin/e7-js.js", import.meta.resolve("@e7lang/e7")));
const dir = mkdtempSync(join(tmpdir(), "e7-js-"));
copyFileSync(new URL("./fixtures/sign.ts", import.meta.url), join(dir, "sign.ts"));
const HMAC = "0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6";

const e7 = (...args) => {
  const r = spawnSync(process.execPath, [bin, ...args], { cwd: dir, encoding: "utf8" });
  return { code: r.status, out: r.stdout, err: r.stderr };
};

test("version / help", () => {
  const v = e7("--version");
  assert.equal(v.code, 0, v.err);
  assert.match(v.out, /^e7-js 1\.0\.1 \(e7 \d+\.\d+\.\d+, WebAssembly\)/);
  assert.match(e7("help").out, /usage: e7-js/);
  assert.equal(e7("bogus").code, 2);
});

test("run", () => {
  let r = e7("run", "sign.ts");
  assert.equal(r.code, 0, r.err);
  assert.equal(r.out, `"${HMAC}"\n`);
  r = e7("run", "sign.ts", "--call", "sign", "amount=100&currency=CNY", "s3cr3t");
  assert.equal(r.out, `"${HMAC}"\n`, r.err);
  writeFileSync(join(dir, "hello.ts"), 'console.log("héllo 👋");\nexport function main(): number { return 6 * 7; }\n');
  r = e7("run", "hello.ts", "--json");
  const j = JSON.parse(r.out);
  assert.deepEqual(j.output, ["héllo 👋"]);
  assert.equal(j.result, 42);
  writeFileSync(join(dir, "spin.ts"), "export function main(): void { while (true) {} }\n");
  r = e7("run", "spin.ts", "--budget", "100000");
  assert.equal(r.code, 1);
  assert.match(r.err, /budget/);
  writeFileSync(join(dir, "boom.ts"), 'export function main(): void { throw new Error("boom"); }\n');
  r = e7("run", "boom.ts");
  assert.equal(r.code, 3);
  assert.match(r.err, /boom/);
});

test("multi-file imports from disk", () => {
  mkdirSync(join(dir, "lib"), { recursive: true });
  writeFileSync(join(dir, "lib", "util.ts"), "export function twice(n: number): number { return n * 2; }\n");
  writeFileSync(join(dir, "app.ts"), 'import { twice } from "./lib/util";\nexport function main(): number { return twice(21); }\n');
  const r = e7("run", "app.ts");
  assert.equal(r.out, "42\n", r.err);
});

test("check", () => {
  let r = e7("check", "sign.ts");
  assert.equal(r.code, 0, r.err);
  writeFileSync(join(dir, "bad.ts"), "// x\nlet x: number = 'a';\n");
  r = e7("check", "sign.ts", "bad.ts");
  assert.equal(r.code, 1);
  assert.match(r.err, /^bad\.ts:2:17: error: .*not assignable/m);
});

test("compile -o + run .e7b + disasm", () => {
  let r = e7("compile", "sign.ts", "-o", "out.e7b");
  assert.equal(r.code, 0, r.err);
  assert.ok(existsSync(join(dir, "out.e7b")));
  assert.ok(readFileSync(join(dir, "out.e7b")).length > 100);
  r = e7("run", "out.e7b", "--call", "sign", "amount=100&currency=CNY", "s3cr3t");
  assert.equal(r.out, `"${HMAC}"\n`, r.err);
  r = e7("compile", "sign.ts");
  assert.ok(existsSync(join(dir, "sign.e7b")), r.err);
  r = e7("disasm", "out.e7b");
  assert.equal(r.code, 0, r.err);
  assert.match(r.out, /func #\d+ sign/);
  assert.equal(e7("disasm", "sign.ts").out, r.out);
  r = e7("compile", "bad.ts");
  assert.equal(r.code, 1);
  assert.match(r.err, /bad\.ts:2:17/);
});
