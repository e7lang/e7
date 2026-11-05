// Bundles the extension and @e7lang/e7 (packages/e7, or E7_PKG_DIR) into dist/ with esbuild.
// Needs the WebAssembly build in <pkg>/wasm/ (e7wasm.js + e7wasm.wasm).
import { build } from "esbuild";
import { copyFileSync, existsSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { join, resolve } from "node:path";

const here = fileURLToPath(new URL("..", import.meta.url));
const pkg = resolve(process.env.E7_PKG_DIR || join(here, "../../packages/e7"));
const wasm = join(pkg, "wasm/e7wasm.wasm");
if (!existsSync(wasm)) {
  console.error(`missing ${wasm}: build the WebAssembly module first (see packages/e7/README.md)`);
  process.exit(1);
}
mkdirSync(join(here, "dist"), { recursive: true });

// dist/extension.js (extension host) and dist/debugWorker.js (worker thread of the debugger)
await build({
  entryPoints: { extension: join(here, "src/extension.js"), debugWorker: join(here, "src/debugWorker.js") },
  outdir: join(here, "dist"),
  bundle: true,
  format: "cjs",
  platform: "node",
  target: "node18",
  external: ["vscode"],
  alias: { "@e7lang/e7": join(pkg, "index.js") },
  // the Emscripten glue is an ES module that uses import.meta.url; give it the bundle's own URL
  define: { "import.meta.url": "__e7_import_meta_url" },
  inject: [join(here, "scripts/import-meta-url.js")],
  minify: process.argv.includes("--minify"),
  sourcemap: false,
  logLevel: "info",
});
copyFileSync(wasm, join(here, "dist/e7wasm.wasm"));
const license = join(here, "../../LICENSE");
if (existsSync(license)) copyFileSync(license, join(here, "LICENSE"));
console.log("built dist/extension.js, dist/debugWorker.js + dist/e7wasm.wasm");
