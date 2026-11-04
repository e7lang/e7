// Fails `npm pack` / `npm publish` when the WebAssembly build has not been copied in.
// Build it with Emscripten (see README.md), then: cp build-wasm/e7wasm.js build-wasm/e7wasm.wasm packages/e7/wasm/
import { existsSync, copyFileSync } from "node:fs";

const dir = new URL("../", import.meta.url);
for (const f of ["wasm/e7wasm.js", "wasm/e7wasm.wasm"]) {
  if (!existsSync(new URL(f, dir))) {
    console.error(`missing ${f}: copy build-wasm/e7wasm.js and build-wasm/e7wasm.wasm (emcmake cmake build) into packages/e7/wasm/`);
    process.exit(1);
  }
}
if (!existsSync(new URL("LICENSE", dir)) && existsSync(new URL("../../LICENSE", dir))) copyFileSync(new URL("../../LICENSE", dir), new URL("LICENSE", dir));
