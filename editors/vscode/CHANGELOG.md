# Changelog

## 0.2.5

- Bundles `@e7lang/e7` 0.2.5: generics, `finally`, class accessors / `abstract` / `#private`, destructuring
  defaults and rest, `console.log` output identical to Node.js.

## 0.2.1

- *Caught errors* exception breakpoint: stop on errors that a `try`/`catch` handler takes, before the handler runs.
  The existing breakpoint is now called *Uncaught errors*.
- Bundles `@e7lang/e7` 0.2.1 (security fixes for `try`/`catch`).

## 0.2.0

- Bundles `@e7lang/e7` 0.2.0: `try` / `catch`, bytecode format 2 (modules built with 0.1 must be recompiled).
- The debugger stops only on runtime errors that no `catch` handles.

## 0.1.0

First public release. Earlier builds were published as 1.0.0–1.0.2 and have been withdrawn;
E7 is versioned 0.x until it has run in production.

- E7 diagnostics, *Run File*, *Compile to Bytecode*, *Show Bytecode*, status bar item,
  `e7.files` / budget / memory settings.
- **Debugger** (debug type `e7`): F5 / *E7: Debug File* on an E7 file, `launch.json` configurations
  (`program`, `call`, `args`, `stopOnEntry`, `budget`, `memoryLimit`), breakpoints, step over / into / out,
  pause (also of infinite loops), call stack, scopes and expandable variables, watch / hover / Debug Console
  evaluation, `debugger;` statements, a *Runtime errors* exception breakpoint, `console.log` output in the
  Debug Console. Runs the script in a worker thread on the bundled WebAssembly VM.
- Bundles `@e7lang/e7` 0.1.0 (E7 compiler + VM on WebAssembly).
