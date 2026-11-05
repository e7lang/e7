# Changelog

## 1.0.2

- **Debugger** (debug type `e7`): F5 / *E7: Debug File* on an E7 file, `launch.json` configurations
  (`program`, `call`, `args`, `stopOnEntry`, `budget`, `memoryLimit`), breakpoints, step over / into / out,
  pause (also of infinite loops), call stack, scopes and expandable variables, watch / hover / Debug Console
  evaluation, `debugger;` statements, a *Runtime errors* exception breakpoint, `console.log` output in the
  Debug Console. Runs the script in a worker thread on the bundled WebAssembly VM.
- Bundles `@e7lang/e7` 1.0.2.

## 1.0.1

- First release: E7 diagnostics, *Run File*, *Compile to Bytecode*, *Show Bytecode*, status bar item,
  `e7.files` / budget / memory settings. Bundles `@e7lang/e7` 1.0.1 (E7 compiler + VM on WebAssembly).
