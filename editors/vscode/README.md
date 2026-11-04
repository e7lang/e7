# E7 Language for VS Code

Editor support for [E7](https://e7lang.github.io), the strict, statically typed TypeScript subset that
runs in the embeddable e7vm. The extension bundles the real E7 compiler and VM compiled to WebAssembly
(the [`@e7lang/e7`](https://www.npmjs.com/package/@e7lang/e7) package), so diagnostics and runs match
`e7c` / `e7` exactly. No native install, no network access.

## Features

- **Diagnostics as you type** (debounced) from the E7 compiler: type errors, unsupported TypeScript
  features (`any`, ...), unknown imports. Relative imports (`./util`) are resolved from disk and from
  open editors.
- **E7 files:** `*.e7` (language `E7`, highlighted with VS Code's TypeScript grammar), `.ts` files that
  import from `"e7:..."` modules, and `.ts` files matching the `e7.files` globs.
- **E7: Run File** runs the top-level code and `main()` in the sandboxed VM and prints `console.log`
  output and `main()`'s result to the *E7* output channel (also on the editor title bar run button).
- **E7: Compile to Bytecode** writes portable bytecode (`.e7b`) next to the file; run it with `e7`,
  load it with `e7_load()` or `@e7lang/e7`'s `run(bytes)`.
- **E7: Show Bytecode** opens the disassembly in a side-by-side read-only document.
- **Status bar** item showing whether the current E7 file compiles; click to run it.

For completion and hover, keep using VS Code's TypeScript support with
[`lib/e7.d.ts`](https://github.com/e7lang/e7/blob/main/lib/e7.d.ts) in your `tsconfig.json`
(see the main README, "Editor setup"); this extension adds the E7 compiler's own checks on top.

## Settings

| Setting | Default | Description |
|---|---|---|
| `e7.files` | `[]` | Extra glob patterns of `.ts` files to treat as E7, e.g. `["**/scripts/**/*.ts"]` |
| `e7.diagnostics.enable` | `true` | Report E7 compiler errors |
| `e7.diagnostics.debounce` | `300` | Delay (ms) before re-checking a changed document |
| `e7.run.budget` | `100000000` | Instruction budget for *Run File* (0 = unlimited) |
| `e7.run.memoryLimit` | `67108864` | VM heap limit (bytes) for *Run File* |

## Building from source

```sh
# 1. WebAssembly build (Emscripten), copied into the npm package
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release && cmake --build build-wasm
cp build-wasm/e7wasm.js build-wasm/e7wasm.wasm packages/e7/wasm/
# 2. Extension
cd editors/vscode
npm ci
npm run build              # dist/extension.js + dist/e7wasm.wasm
xvfb-run -a npm test       # integration tests in a downloaded VS Code
npx vsce package --no-dependencies
```

Prebuilt `.vsix` files are attached to every [GitHub release](https://github.com/e7lang/e7/releases):
install with *Extensions: Install from VSIX...* or `code --install-extension e7-1.0.1.vsix`.

## License

MIT
