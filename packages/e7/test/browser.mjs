// Browser smoke test: serves the (installed) package over HTTP and drives it in headless Chromium.
//   npm install --no-save playwright@1.63.0 && npx playwright@1.63.0 install --with-deps chromium
//   node test/browser.mjs
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { join, normalize } from "node:path";
import { chromium } from "playwright";

const root = fileURLToPath(new URL("./", import.meta.resolve("@e7lang/e7")));
const sign = await readFile(new URL("./fixtures/sign.ts", import.meta.url), "utf8");
const HMAC = "0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6";

const page = `<!doctype html><meta charset="utf-8"><title>e7 browser test</title>
<script type="module">
  import { init, run, compile, check, disassemble } from "/pkg/index.js";
  const out = {};
  try {
    const e7 = await init();
    out.version = e7.version;
    const src = ${JSON.stringify(sign)};
    out.main = (await run(src)).result;
    const c = await compile(src, { fileName: "sign.ts" });
    out.call = (await run(c.bytecode, { call: "sign", args: ["amount=100&currency=CNY", "s3cr3t"] })).result;
    out.diag = (await check("let x: number = 'a';"))[0];
    const r = await run('console.log("héllo 👋"); export function main(): void { while (true) {} }', { budget: 1e5 });
    out.output = r.output;
    out.budget = r.status;
    out.disasm = /func #\\d+ main/.test(await disassemble(src));
  } catch (e) {
    out.error = String(e && e.stack || e);
  }
  window.__e7 = out;
</script>`;

const types = { ".js": "text/javascript", ".wasm": "application/wasm", ".html": "text/html", ".json": "application/json" };
const server = createServer(async (req, res) => {
  const url = new URL(req.url, "http://x");
  if (url.pathname === "/") {
    res.writeHead(200, { "content-type": "text/html; charset=utf-8" });
    return res.end(page);
  }
  if (url.pathname.startsWith("/pkg/")) {
    const file = normalize(join(root, decodeURIComponent(url.pathname.slice(5))));
    if (file.startsWith(root)) {
      try {
        const body = await readFile(file);
        res.writeHead(200, { "content-type": types[file.slice(file.lastIndexOf("."))] ?? "application/octet-stream" });
        return res.end(body);
      } catch {}
    }
  }
  res.writeHead(404);
  res.end("not found");
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const { port } = server.address();

const browser = await chromium.launch();
let failed = false;
try {
  const p = await browser.newPage();
  const logs = [];
  p.on("console", (m) => logs.push(m.text()));
  p.on("pageerror", (e) => logs.push(String(e)));
  await p.goto(`http://127.0.0.1:${port}/`);
  await p.waitForFunction(() => window.__e7, null, { timeout: 60000 });
  const r = await p.evaluate(() => window.__e7);
  console.log(JSON.stringify(r, null, 2));
  const check = (cond, what) => {
    if (!cond) {
      failed = true;
      console.error("FAIL:", what);
    }
  };
  check(!r.error, `no exception (${r.error}) ${logs.join("\n")}`);
  check(/^\d+\.\d+\.\d+/.test(r.version ?? ""), "version");
  check(r.main === HMAC, "main() HMAC");
  check(r.call === HMAC, "sign() from bytecode");
  check(r.diag && r.diag.line === 1 && r.diag.column === 17 && r.diag.severity === "error", "diagnostic");
  check(JSON.stringify(r.output) === JSON.stringify(["héllo 👋"]), "output");
  check(r.budget === "budget_exceeded", "budget");
  check(r.disasm === true, "disassemble");
} finally {
  await browser.close();
  server.close();
}
if (failed) process.exit(1);
console.log("browser test OK");
