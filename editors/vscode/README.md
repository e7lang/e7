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
- **Debugger** (debug type `e7`): breakpoints, step over / into / out, pause, call stack, scopes and
  expandable variables, hover / watch / Debug Console evaluation, `debugger;` statements and stops on
  runtime errors. Press **F5** in an E7 file (no `launch.json` needed), use **E7: Debug File** (editor
  title bar and context menu), or add a launch configuration. See [Debugging](#debugging).

For completion and hover, keep using VS Code's TypeScript support with
[`lib/e7.d.ts`](https://github.com/e7lang/e7/blob/main/lib/e7.d.ts) in your `tsconfig.json`
(see the main README, "Editor setup"); this extension adds the E7 compiler's own checks on top.

## Debugging

Open an E7 file and press **F5** (or run **E7: Debug File**): the file's top-level code runs, then
`main()` if it is exported. Set breakpoints in `.e7` files and in `.ts` files of the program (imported
files too). To debug another exported function, with arguments, add a configuration to
`.vscode/launch.json`:

```jsonc
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "e7",
      "request": "launch",
      "name": "Debug sign()",
      "program": "${workspaceFolder}/scripts/sign.ts",
      "call": "sign",               // exported function (default: main, if exported)
      "args": ["hello", 42],        // JSON arguments for `call`
      "stopOnEntry": false,         // stop at the first statement
      "budget": 0,                  // instruction budget, 0 = unlimited
      "memoryLimit": 67108864       // VM heap limit in bytes
    }
  ]
}
```

| Feature | Notes |
|---|---|
| Breakpoints | Stop at the first statement on or after the line; a breakpoint moves to that line once the code is loaded. No conditions / hit counts / logpoints yet. |
| Stepping | *Step Over*, *Step Into* (also into closures), *Step Out* (stops right after the call, mid-statement). |
| Pause | Stops at the next statement, also in infinite loops (in functions and in top-level code). |
| Variables | *Locals* (parameters, block-scoped variables live at that point), *Closure* (captured variables), *Globals*. Arrays, objects, class instances, `Map`, `Set`, tuples, `Uint8Array` and function closures expand; large arrays are paged. |
| Evaluate | Watch, hover and Debug Console accept a variable followed by `.field`, `[index]`, `['map key']`, `.length`, `.size` (no operators or calls). |
| Runtime errors | The *Runtime errors* exception breakpoint (on by default) stops before an error terminates the script, with all frames intact; continuing ends the script with the error. |
| Output | `console.log` goes to the Debug Console; the called function's result and runtime errors are printed when the script ends. |

The script runs in the same WebAssembly VM as *Run File*, in a worker thread (VS Code never blocks, and
*Stop* always ends the script). Unsaved changes in open editors are used. Values cannot be modified, and
stops inside callbacks called by natives (an `Array.sort` comparator, an `assertThrows` body) are
skipped. Details: [docs/DEBUGGING.md](https://github.com/e7lang/e7/blob/main/docs/DEBUGGING.md).

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
npm run build              # dist/extension.js, dist/debugWorker.js + dist/e7wasm.wasm
xvfb-run -a npm test       # integration tests in a downloaded VS Code
npx vsce package --no-dependencies
```

Prebuilt `.vsix` files are attached to every [GitHub release](https://github.com/e7lang/e7/releases):
install with *Extensions: Install from VSIX...* or `code --install-extension e7-0.1.0.vsix`.

## License

MIT
