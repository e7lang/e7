# @e7lang/e7

The [E7](https://e7lang.github.io) compiler and sandboxed VM compiled to WebAssembly, for Node.js (≥ 18)
and browsers. E7 is a strict, statically typed TypeScript subset; this package type checks E7 source,
compiles it to portable bytecode (`.e7b`, the same files `e7c` produces), disassembles it and runs it
with an instruction budget and a memory limit. It is the same engine as the native e7vm, the
[playground](https://e7lang.github.io/playground) and the VS Code extension.

```sh
npm install @e7lang/e7
```

```js
import { run, compile, check } from "@e7lang/e7";

const src = `
import { hmac } from "e7:crypto";
import { hexEncode } from "e7:encoding";
export function sign(message: string, secret: string): string {
  const enc = new TextEncoder();
  return hexEncode(hmac("sha256", enc.encode(secret), enc.encode(message)));
}
console.log("loaded");
`;

const r = await run(src, { call: "sign", args: ["amount=100&currency=CNY", "s3cr3t"] });
// { ok: true, status: "ok", code: 0, output: ["loaded"],
//   result: "0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6", error: null }

const { ok, bytecode, diagnostics } = await compile(src, { fileName: "sign.ts" });
await run(bytecode, { call: "sign", args: ["amount=100&currency=CNY", "s3cr3t"] }); // same result

await check("let x: number = 'a';", { fileName: "bad.ts" });
// [{ file: "bad.ts", line: 1, column: 17, severity: "error",
//    message: "type 'string' is not assignable to type 'number'" }]
```

## API

All module-level functions return Promises and load the WebAssembly module on first use. For a
synchronous API, `const e7 = await init()` and call the same methods on `e7` (`e7.run(...)`, ...).

| Function | Returns |
|---|---|
| `init(options?)` | `Promise<E7>`: synchronous instance; `e7.version` is the VM version. Options: `wasmBinary`, `wasmURL`, `locateFile` |
| `compile(source, { fileName?, files?, readFile?, debug?, target? })` | `{ ok, bytecode?: Uint8Array, diagnostics: Diagnostic[] }`; `target: "0.2"` = the module must run on e7vm 0.2 (newer features are errors) |
| `check(source, options?)` | `Diagnostic[]` (empty when the file compiles) |
| `run(sourceOrBytecode, { budget?, memoryLimit?, seed?, call?, args?, ...compileOptions })` | `{ ok, status, code, output: string[], result, error, diagnostics? }` |
| `disassemble(sourceOrBytecode, options?)` | bytecode listing (throws `E7Error` with `.diagnostics` on compile errors) |
| `listExports(sourceOrBytecode, options?)` | `[{ name, params: ["string", "i32", ...], returns }]` |
| `inspect(bytecode)` | `{ ok, status, code, format, requiresVersion: "0.2.0", requiresVersionNumber, signed, keyId, debugInfo }`: the module header, without loading it (`status` `"version_error"` = this VM is too old) |
| `createDebugSession({ entry, source?, files?, readFile?, call?, args?, memoryLimit?, budget?, slice?, breakOnError? })` | `Promise<DebugSession>`: a debugger on its own WebAssembly instance (see below) |
| `formatDiagnostics(diagnostics)` | `"file:line:column: error: message"` lines |
| `version` | version of this package |

- **Diagnostic**: `{ file, line, column, message, severity }`, 1-based; `column` counts UTF-16 code
  units (like JavaScript string indices), so it maps directly onto editor positions.
- **run**: executes the top-level statements, then `call` with `args` (default: `main()` if exported).
  `output` has one entry per `console.log`. `status` is one of `ok`, `runtime_error`,
  `budget_exceeded`, `out_of_memory`, `stack_overflow`, `load_error`, `verify_error`,
  `signature_error`, `not_found`, `bad_arguments`, `compile_error`, `version_error` (the module needs a
  newer VM) (`code` is the numeric `e7_status` from `e7.h`). `budget` defaults to 100,000,000 instructions (0 = unlimited),
  `memoryLimit` to 64 MiB; `seed` makes `Math.random` deterministic.
- **async**: `call` may name an `async function f(): Promise<T>`; `result` is the promise's value (a
  rejection gives `runtime_error`). Top-level `await` works too: `run` drains the promise jobs before it
  returns. This package registers no async host functions (the embedding APIs do: see
  [Async host functions](https://github.com/e7lang/e7/blob/main/docs/EMBEDDING.md#async-host-functions)),
  so the C API's `pending` status (15) does not occur here; `e7:test`'s `later(value)` / `laterReject(message)`
  stand in for host operations in tests.
- **Values**: `number`, `boolean`, `string`, `Uint8Array` and `null` map directly. Integer parameters
  (`i8` ... `u64`) accept integral numbers or `bigint`; `i64`/`u64` results are returned as `bigint`,
  smaller integer types as `number`.
- **Imports**: relative imports (`import { f } from "./util"`) are resolved from `files` (a map of
  paths relative to the entry file) and/or a synchronous `readFile(path)` callback, e.g.
  `readFile: (p) => fs.readFileSync(p, "utf8")`. `e7:*` standard modules are built in.
- Every `run` uses a fresh VM; nothing is shared between runs.
- **Debugging**: a `DebugSession` has `setBreakpoints(file, lines)`, `launch({ stopOnEntry })`,
  `continue()`, `next()`, `stepIn()`, `stepOut()` (each resolves with a `stopped` or `terminated` event),
  `pause()`, `stack()`, `scopes(frameId)`, `variables(ref, start, count)`, `evaluate(frameId, "a.b[0]")`,
  `terminate()` and `on("output" | "stopped" | "terminated", fn)`. Scripts run in time slices, so `pause()`
  stops infinite loops. See [docs/DEBUGGING.md](https://github.com/e7lang/e7/blob/main/docs/DEBUGGING.md#82-javascript-createdebugsession).

## Command line

`e7-js` offers the native `e7` / `e7c` workflow without a native build:

```sh
npx -p @e7lang/e7 e7-js run sign.ts                            # top level + main(), prints the result
npx -p @e7lang/e7 e7-js run sign.ts --call sign "amount=100&currency=CNY" s3cr3t
npx -p @e7lang/e7 e7-js check src/*.ts                         # diagnostics, exit code 1 on errors
npx -p @e7lang/e7 e7-js compile sign.ts -o sign.e7b            # portable bytecode
npx -p @e7lang/e7 e7-js disasm sign.e7b
```

`run` options: `--call <fn>` (arguments are parsed as JSON when possible, else passed as strings),
`--budget <n>`, `--mem <bytes>`, `--seed <n>`, `--json`.

## Browsers and bundlers

The package is a plain ES module. `e7wasm.wasm` is located next to `wasm/e7wasm.js` through
`new URL(..., import.meta.url)`, which Vite, webpack 5, Rollup and esbuild understand; it is also
exported as `@e7lang/e7/e7wasm.wasm` if you prefer to serve it yourself and pass `init({ wasmURL })`
or `init({ wasmBinary })`. For CommonJS bundles (e.g. VS Code extensions) define `import.meta.url` or
pass `wasmBinary`; see `editors/vscode/scripts/build.mjs` in the repository.

## Building from source

```sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release && cmake --build build-wasm
cp build-wasm/e7wasm.js build-wasm/e7wasm.wasm packages/e7/wasm/
cd packages/e7 && npm test && npm pack
```

## License

MIT
