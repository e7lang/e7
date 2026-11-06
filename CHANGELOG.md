# Changelog

All notable changes to E7 / e7vm are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

**Versioning while E7 is 0.x**: a minor release (0.1 → 0.2) may contain incompatible changes to the language,
the C API or the bytecode format; each one is listed under *Changed* or *Removed* with what to do. A patch
release (0.1.0 → 0.1.1) is compatible. The bytecode format keeps its own version number in the `.e7b` header,
and a VM rejects a module it cannot run at load time rather than running it incorrectly.

## [Unreleased]

## [0.3.0] - 2026-11-06

The language complete for its domain (docs/ROADMAP.md, section 0.3; design decisions in docs/DESIGN-0.3.md).
Bytecode format 3 (docs/BYTECODE.md, section 6) for modules that use generators, `async`, regular expressions or
records with a presence mask; every other module is still format 2 and runs on 0.2.x VMs. 0.2 modules run on this
VM unchanged (the backward-compatibility CI job checks the suite compiled by 0.2.6). docs/TYPESCRIPT.md: 153 of 250
features supported (129 in 0.2.7), 18 partial, 79 unsupported.

### Added
- Generators: `function*`, `yield`, `yield*`, generator methods, `Generator<T, TReturn, TNext>` / `Iterator<T>` /
  `IterableIterator<T>`, `next` / `return` / `throw`; `for...of`, spread, `Array.from` and destructuring over
  generators and classes with a `*[Symbol.iterator]()` method (leaving a loop early calls `return()`). New
  instructions `GEN_START`, `YIELD`, `RESUME_I/F/R`; a generator is a coroutine that keeps its frame on the heap.
- `async` / `await`: async functions, methods and arrow functions, `await`, top-level `await` in the entry module,
  `for await`, `Promise<T>` with `then` / `catch` / `finally`, `new Promise`, `Promise.resolve` / `reject` / `all` /
  `allSettled` / `any` / `race`. The VM runs the specification's promise jobs, so callbacks run in the order of
  Node.js. New instructions `ASYNC_START`, `AWAIT`.
- Host async functions (C API): `declare function f(...): Promise<T>` registered with
  `e7_register_async_native`; the host settles tickets with `e7_settle` / `e7_settle_error`; a call returns the new
  status `E7_PENDING` while tickets are open and continues with `e7_resume`; `e7_open_tickets`. An exported async
  function gives the host its promise's value.
- Async host functions in every binding (docs/EMBEDDING.md §2.6): Java `registerAsyncNative` with `callAsync`
  (a `CompletableFuture`, completions settled on a serial executor) and the blocking `callAwait` / `loadAwait`
  (Kotlin: `callAsync(...).await()`); Swift `registerAsyncNative` with `callAsync` / `loadAsync` (`async`, on the
  caller's actor); C# `RegisterAsyncNative` with `CallAsync` / `LoadAsync` (`Task`); the JavaScript package reports
  the `pending` status. The module managers run async exports (Java also awaits top-level code and `selfTest`).
- C# `E7ModuleManager` (in the `E7VM` NuGet package): signed manifests, staged rollout, kill switch, `minVm`,
  `selfTest`, last-known-good with rollback after consecutive failures, as the Java and Swift managers
  (docs/DEPLOYMENT.md); `ActiveModule.Call` / `CallAsync` / `CallAwait`. `E7VM.CallAwait` / `LoadAwait` block
  until an async call finishes (the manager uses them for top-level code and `selfTest`).
- Regular expressions: literals (checked at compile time, stored in the module's table and compiled when it is
  loaded), `new RegExp(source, flags)`, flags `dgimsuy`, named groups, lookbehind, Unicode property escapes;
  `test` / `exec` / `lastIndex`, `String` `match` / `matchAll` / `replace` / `replaceAll` (replacement patterns
  and callbacks) / `split` / `search`; match results print as in Node.js. An own backtracking engine
  (`src/regexp/`, ECMAScript semantics, V8's error messages) whose every step is charged to the instruction budget
  and whose backtracking memory to the memory limit: a catastrophic pattern ends with `E7_ERR_BUDGET` instead of
  blocking the host.
- Literal types: string, numeric and boolean literal types and unions of literals of one base type, narrowing by
  `===` / `!==` / `switch`, exhaustive switches whose `default` sees `never`, `as const`, `const` declarations with
  literal types; `Awaited`, `NonNullable`, `NoInfer`, `Uppercase` / `Lowercase` / `Capitalize` / `Uncapitalize`.
- Type predicates (`x is T`, `this is T`) and assertion functions (`asserts x is T`, `asserts x`).
- Computed property names with keys known at compile time (`{ [KEY]: v }`, class and interface members, `obj[KEY]`),
  tagged templates and `String.raw`, `<T>x` assertions, hashbang lines, `declare` fields, overrides with fewer
  parameters or a covariant return type, the `this` return type of methods, interface declaration merging, enum
  members as types, built-in functions and functions with default parameters as values where a function type is
  expected (`xs.map(Math.sqrt)`, `strs.map(Number)`, `xs.filter(Boolean)`).
- `JSON.parse(text) as T` / `tryParse` into recursive types.
- Standard library: `toSorted` / `toReversed` / `toSpliced` / `with`; `Date` setters (`setFullYear` ...
  `setMilliseconds` and the UTC variants); `Error` `cause` (`new Error(msg, { cause })`) and `AggregateError` (also
  from `Promise.any`); typed arrays `Int8Array` ... `Float64Array`, `Uint8ClampedArray`, `BigInt64Array`,
  `BigUint64Array`; `structuredClone` of statically typed values.
- `e7:test`: `later(v)` / `laterReject(e)`, promises settled in a later turn (tests of asynchronous ordering).
- `console.log` of promises (`Promise { 1 }`) and generators (`Object [Generator] {}`), as Node.js prints them.

### Changed
- A property that holds `undefined` is an own property, as in JavaScript: records whose type has optional or
  `| undefined` properties carry a presence mask (format-3 class flag, instruction `FIELD_ABSENT`), so `Object.keys`,
  spread and `console.log` show `x: undefined` where Node.js does, and leave out properties an object literal did
  not set. `e7c --target 0.2` keeps the 0.2 behaviour (format 2).
- Literal types are checked: `let m: "GET" | "POST" = s` with a `string` `s` is now a compile error, as in tsc
  (0.2 treated literal types as `string`). Programs that type-check with tsc are not affected.
- An uncaught error of a call runs the promise jobs it queued before the call fails, as in JavaScript.
- `e7_load` / `e7_call` / `e7_resume` may return `E7_PENDING`; hosts that do not register async natives never see it.

### Security
- Two independent reviews of the new VM code (docs/PRODUCTION.md 3.7 and 3.8): typed-array, promise and
  `structuredClone` hardening, and budget / memory-limit accounting of regular expression compilation, replacement
  and per-match work. No memory-safety issue was found in the regular expression engine (about 2.9 million fuzz
  cases under ASan / UBSan).

### Not in 0.3
- Async generators (`async function*`) and getters / setters in object literals: see docs/DESIGN-0.3.md,
  implementation notes.

## [0.2.7] - 2026-11-06

Compiler-only gaps from the 0.3 plan (docs/ROADMAP.md, item D). The VM, the bytecode format and the C API are
unchanged: modules this compiler builds run on every 0.2.x VM that runs the 0.2.6 compiler's output (the
forward-compatibility CI jobs check v0.2.6, v0.2.5, v0.2.1 and v0.2.0). docs/TYPESCRIPT.md: 129 of 250 features
supported (117 in 0.2.6), 16 partial, 105 unsupported.

### Added
- Recursive interfaces and object type aliases, also mutually recursive and generic ones
  (`interface Tree { children: Tree[] }`, `interface List<T> { tail: List<T> | null }`).
- Destructuring assignment with object patterns, defaults, nesting and rest, as a statement
  (`({ host, port = 80, ...rest } = options);`, `[first, ...others] = xs;`).
- `||=` and `&&=`; the comma operator; the `void` operator.
- Constant expressions in enum members (`ReadWrite = Read | Write`, `High = Low << 8`, `B = A + "-b"`).
- Private accessors (`get #x()` / `set #x(v)`, instance and static).
- Narrowing by a conditional expression with non-null branches (`x = x === null ? [] : x`) and of static field paths
  (`if (C.#instance === null) C.#instance = new C(); return C.#instance;`).
- Modules: default exports and imports (`export default`, `import x from`, `export { a as default }`), re-exports
  (`export { x } from`, `export * from`, `export * as ns from`), side-effect imports (`import "./x"`), and classes,
  types and enums through namespace imports (`new ns.C()`, `ns.E.A`, `let b: ns.Box<number>`). Modules are
  initialized depth first in source order, as in JavaScript.

### Fixed
- `import type` and `export type ... from` are erased as in TypeScript: a module reached only through them is no
  longer run, and a class or enum imported with `import type` cannot be used as a value (tsc's TS1361 error).
- `KeyHandle` can be named through any namespace import of `e7:crypto` (`import * as c from "e7:crypto"; c.KeyHandle`).
- `JSON.parse(text) as T` into a recursive type is a clear compile error (planned for e7vm 0.3) instead of not
  terminating.

## [0.2.6] - 2026-11-06

The subset defined (docs/ROADMAP.md, section 0.2.6). Compatible with 0.2.5: the bytecode format and the C API are
unchanged, modules built by 0.2.5 run on this VM and modules this compiler builds run on 0.2.5 VMs (the
forward-compatibility CI job checks it). The compiler is stricter where it used to accept programs that tsc rejects
(`readonly`, `exactOptionalPropertyTypes`): see *Changed*.

### Added
- [docs/TYPESCRIPT.md](docs/TYPESCRIPT.md): 250 TypeScript features (the TypeScript Handbook's chapters plus the
  JavaScript syntax and standard library) with their status in E7, the reason for each restriction and what to write
  instead. Every row is a sample program in `tests/ts-features` that CI compiles with E7, runs against Node.js when
  supported, and type-checks with tsc 5.6; the table is generated from the samples (`scripts/gen-ts-features.py`).
- Optional interface methods (`m?(): T`), called with `o.m?.()` or after a check; optional function-typed properties
  and fields can be called after a check (`if (o.f) o.f(x)`).
- A local function value may call itself (`const f = (n: number): number => ... f(n - 1)`), and a named function
  expression may call itself through its name.
- A `switch` with a case for every enum member is exhaustive (no `default` or final `return` needed).
- Narrowing by an assignment that ends an `if` branch survives the `if` (`if (x === null) x = []; return x;`).
- SPEC section 0.1 defines the subset: TypeScript 5.6 `tsc` with `strict` and `exactOptionalPropertyTypes`
  (`tests/tsc/tsconfig.json`) is the upper bound, ECMAScript as implemented by Node.js 22 is the run-time behaviour,
  and three rules select the features, with **types known at compile time** as the fixed rule that is never relaxed.

### Changed
- Optional properties and fields follow tsc's `exactOptionalPropertyTypes`: `x?: T` may be left out but not set to
  `undefined` (by an object literal, an assignment, or a spread of a property that may hold `undefined`). Programs
  that did so are now compile errors, as they are for tsc with that option; declare `x?: T | undefined` to keep them.
- `scripts/macos.sh release` runs the full macOS test first (unless it already passed at the same commit with
  nothing skipped).
- `readonly` is checked as in TypeScript beyond class fields: a `readonly` property of an interface or object type
  cannot be assigned, and `readonly T[]` / `ReadonlyArray<T>`, `readonly [A, B]`, `ReadonlyMap` and `ReadonlySet`
  have no mutating methods or element writes and are not assignable to the mutable types. Programs that relied on
  E7 ignoring `readonly` are now compile errors, as they are for tsc.
- Clearer errors for unsupported TypeScript: type predicates, assertion functions, decorators on members,
  auto-accessors, call and construct signatures, constructor types, conditional and indexed access types, rest and
  optional tuple elements, hashbang lines, `Symbol`, `Intl`, and `new` through a namespace import.

### Security
Fixes from an independent review of the 0.2.5 VM changes (docs/PRODUCTION.md section 3.6); none is a memory-safety issue.
- With `suspend_on_budget`, a native that charges its work up front or as it goes (`pbkdf2`, `assertEqual`) could run
  without bound inside one slice (a 0.2.5 regression). It may now run past the end of the slice by at most 2^22 ticks
  (or the slice, when that is larger); past it the call fails with `E7_ERR_BUDGET`.
- `JSON.stringify` and console.log's `%j` check their output against the memory limit as it grows (an object graph
  with many shared nodes could use gigabytes of host memory under a small limit).
- console.log charges its formatting to the instruction budget: the text it produces, `%j`, and the quadratic decimal
  conversion of BigInts.
- The string keys of a default `Array.sort()` are checked against the memory limit and their comparison is charged.
- `parseInt` with a power-of-two radix on hundreds of millions of digits overflowed a signed counter (now Infinity, as in
  JavaScript); a `Map` iteration cursor from crafted bytecode overflowed a signed subtraction. Both were undefined
  behaviour without memory access.

### Fixed
- Default parameters of function declarations, methods and constructors may refer to earlier parameters and `this`
  (SPEC section 5 said so; only arrow functions and function expressions accepted it).
- A method signature mismatch in an override was reported without a location.
- A generic instantiated with `void` (for example `run(() => { ... })` with `run<T>(f: () => T)`) could store or pass
  the void result and crash the compiler with an internal error; storing it in a property or passing it as an
  argument is now a compile error, since E7 has no void values.

## [0.2.5] - 2026-11-06

Stability, reliability and TypeScript compatibility ([docs/ROADMAP.md](docs/ROADMAP.md), section 0.2.5). Compatible
with 0.2.0 / 0.2.1: their modules keep running, and modules this compiler builds run on 0.2.0 / 0.2.1 VMs unless they
use `#private` class fields (`requires 0.2.5`, rejected there with `E7_ERR_VERSION`). Of 59 common TypeScript
patterns probed before this release, 33 were rejected; 14 still are (unions other than with `null`, `Record` and
index signatures, `unknown`, `keyof`, regular expressions, generators, overloads, `in`, `Intl`).

### Added
- **Generics**: generic functions, arrow functions, classes (including generic methods and inheritance), interfaces and
  type aliases, with constraints, defaults, explicit type arguments and tsc-style inference; compiled by
  monomorphization, so no VM change (SPEC 2.3). A type parameter with nothing to infer from takes its default, else
  its constraint; `[]` arguments take their element type from the other arguments.
- **`finally`**, with JavaScript semantics for `return` / `break` / `continue` / catchable errors and overriding
  completions (E7-specific uncatchable errors still end the call without running it).
- Classes: getters and setters, `abstract` classes and members, `#private` fields and methods, `static {}` blocks;
  static members are inherited by subclasses; `String(x)`, template literals and `+` call a class's own `toString()`.
- Default parameters in arrow functions and function expressions; defaults and rest in array / object destructuring
  (declarations, parameters, `for...of`); `a?.[i]`; spreading tuples and arrays into calls;
  `Array.from({ length: n }, fn)`; method shorthand in object literals (with `this`); tuples inferred from
  `const [a, b] = [1, "x"]`.
- Library: `flatMap`, `flat`, `Object.entries` / `values` / `assign`, `replace` / `replaceAll` with a function.

### Changed
- **`console.log` prints exactly like Node.js** (`util.inspect`): multi-line layout, aligned number columns, `... N
  more items`, quoting, `Map(n) {...}`, class names, depth limits, cycles, format strings (`%s %d %i %f %j %o %O %c`).
- **Math functions and `**` are bit-identical to Node.js** on every platform (V8's fdlibm port, `third_party/fdlibm`);
  builds use `-ffp-contract=off`, and 32-bit x86 builds use SSE2 instead of x87.
- Numbers: `toString(radix)`, shortest formatting, `parseInt` above 2^53, `(-0).toFixed()`; Dates before year 0 and
  after 9999, `getDay()` before 1970, `Date.parse` of RFC formats; `encodeURI` of NUL — all now as in V8.
- Map / Set: iteration visits entries added after compaction or `clear()`; `-0` keys are stored as `+0`; `Date` keys
  are compared by identity. `Object.keys` / `JSON.stringify` list array-index keys first; object spread and rest keep
  the source's run-time property order; `Object.keys` lists class fields holding `undefined`.
- The compiler no longer rejects comparisons with `null` / `undefined` that tsc accepts ("always true/false").
- Optional chains short-circuit as in TypeScript: `a?.b.c`, `a?.b[i]` and `a?.m().n()` are `undefined` when `a` is
  (before, the links after `?.` were rejected as accesses on a possibly undefined value).

### Fixed
- Found by the new stability checks: a data race and leak in the compiler on static class fields (two threads
  compiling at once); a failed `e7_load` kept the module and everything its globals reached for the life of the VM;
  no garbage collection during callbacks from natives (a sort comparator's garbage piled up until the native returned);
  natives that charge work up front failed with `E7_ERR_BUDGET` in suspend mode.
- Found by differential fuzzing: internal compiler errors for a default that reads a parameter captured by a closure
  and for chained `map` calls in a `Set` constructor; `new Set<number>(Array.of(...))` was rejected.

### Reliability (CI and tooling)
- GC stress mode (`E7_GC_STRESS`), allocation-failure injection with a heap consistency check, equivalence runs
  (budget slice 1, a stepping debugger, switch dispatch), a 100,000-call soak test, a multi-thread test under TSan,
  forward-compatibility runs of new modules on the previous release's VM, and a coverage report (lines 93.7%).
- Differential fuzzing: random E7 programs run on e7 and Node.js and compared (`tests/difffuzz`; a smoke test in
  ctest and a nightly workflow); about 70,000 programs were compared during this release.
- macOS and iOS are built and tested on a maintainer's Mac (`scripts/macos.sh test`), and the macOS release files are
  built there (`scripts/macos.sh release`) and published by the release workflow; GitHub Actions runs Linux, Windows,
  Android and WebAssembly. The macOS archive is not covered by build provenance attestations.
- XCFramework: iOS 15.0 by default (`IOS_DEPLOYMENT_TARGET` overrides).

## [0.2.1] - 2026-11-05

### Added
- Debuggers can stop on errors that a `try`/`catch` handler takes: `e7_debug_break_on_caught_errors()`,
  `e7w_dbg_break_on_caught()`, `breakOnCaught` / `setBreakOnCaught()` in `@e7lang/e7`, and the *Caught errors*
  exception breakpoint in VS Code (the existing one is now called *Uncaught errors*). The synchronous handler stops
  where the error is raised; pause-to-host stops before the handler with the error's frames still on the stack.

### Security
Fixes from an independent review of try/catch (details in [docs/PRODUCTION.md §3.5](docs/PRODUCTION.md)); no
memory-safety issue was found.
- The verifier rejects try/catch handlers at or before their range (a crafted module could loop without spending
  budget).
- Library functions that fail are charged for their work (a loop of failing `JSON.parse` calls could run for hours on
  a small budget); catching charges one tick per dropped frame and no longer builds an unused stack trace.
- An out-of-memory error inside `Map.set` no longer corrupts the VM's heap accounting.

### Changed
- e7's own size limits (JSON and array-to-string nesting, BigInt size, array and typed-array lengths) are no longer
  reported as `RangeError` / `SyntaxError`, so `catch` does not see them; the messages say "an e7 limit".
- Error values convert like in JavaScript everywhere: `JSON.stringify(e)` is `{}`, `String([e])` and `${optionalError}`
  give `"Name: message"`; `instanceof Error` is false for holes of `new Array<Error>(n)`; `console.log` of a value
  that contains an Error is a compile error. More than 65,535 `try` statements in a function is a compile error.

## [0.2.0] - 2026-11-05

The release for production pilots: the incompatible changes we could foresee (bytecode format, C ABI, exceptions)
are made now, so that the 0.2.x line can stay compatible. See [docs/ROADMAP.md](docs/ROADMAP.md).

### Added
- **`try` / `catch`** ([SPEC.md 3.1](docs/SPEC.md)). `catch (e)` (an `unknown`, narrowed with `e instanceof Error`,
  `TypeError`, `RangeError`, `SyntaxError`, `URIError`) and `catch` without a binding. Catches the errors Node.js
  also throws: `throw`, `JSON.parse` syntax errors and the standard library's `RangeError` / `TypeError` /
  `URIError`s; E7-specific checks (out-of-bounds reads, `as T` shape mismatches, ...), `abort()` and resource
  limits still end the call, so programs keep printing what Node.js prints. `Error` values have `.name` and
  `.message`; `String(e)` gives `"Name: message"`; `new SyntaxError` / `new URIError` are available.
  Bytecode: a per-function handler table (checked by the verifier) and a `THROW` instruction. Debuggers stop only
  on errors no handler catches.
- **Version targeting**: modules record the minimum e7vm version they need (`requires`, computed from the
  instructions and natives they use); `e7c --target 0.2` refuses newer features; `e7c --info` prints a module's
  format, required version, signature key id and exports.
- C API: `E7_ERR_VERSION` (format or required version not supported), `e7_inspect()` (header, required version and
  signature without loading), `e7_version_number()`, `e7_gc_stats_get()` (collections, total and longest pause,
  heap size and peak), `e7_config.gc_growth_percent` (heap growth before the next collection, default 200),
  `e7_compile_options.target_version`, `e7_compile_options_init()`.
- Compatibility corpus `tests/compat/`: 0.1 modules must be rejected with `E7_ERR_VERSION`; modules compiled by
  0.2.0 (`tests/compat/0.2.0/`) must keep running with the same output on every 0.2.x VM.
- **Over-the-air delivery**: `E7ModuleManager` for Kotlin/Java and Swift (signed manifest, rollout percentage, kill
  switch, `selfTest()` before activation, rollback to the last good module, bundled fallback, error reports);
  detached Ed25519 signatures for manifests (`e7_sign_detached` / `e7_verify_detached`, `e7c --sign-file` /
  `--verify-file`); `tools/manifest.py`; [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md).
- Bindings: version number, `inspect`, GC statistics and `gcGrowthPercent` in Java/Kotlin, Swift and C#; `target`
  and `inspect()` in `@e7lang/e7`.
- **Measurements and gates**: `e7 --stats` and `e7_ticks_used()` (instruction ticks, GC pauses, peak heap); a CI
  performance gate (exact ticks and peak heap per benchmark, cachegrind instruction count within 3%); a GC pause
  benchmark (`bench/gc_pause.cpp`); `fuzz_natives`, a fuzzer for the crypto and encoding natives.
- Releases carry an SPDX SBOM and GitHub build provenance attestations for every file and package.

### Fixed
- `atoi` was used without `<cstdlib>` (fails with newer libc++); BearSSL's hash vtables are built with
  `-fno-sanitize=function` so the UBSan fuzzing jobs do not stop on their generic function pointers.

### Changed (incompatible)
- **Bytecode format 2.** 0.1 modules are rejected with `E7_ERR_VERSION`: recompile them with e7c 0.2.
- **`throw` takes Errors only**: `throw "text"` is a compile error; write `throw new Error("text")`. An uncaught
  `TypeError` / `RangeError` / ... reports `"TypeError: message"`; an uncaught `Error` still reports the message.
- **C ABI**: `e7_config` and `e7_compile_options` start with a `struct_size` field. Initialize them with
  `e7_config_init()` / `e7_compile_options_init()` before setting fields (a struct without a valid `struct_size`
  is rejected: `e7_vm_new` returns NULL, `e7_compile_ex` returns `E7_ERR_ARGS`). The bindings are updated.
- `Array.reduce` on an empty array without an initial value throws a catchable `TypeError` (as in JavaScript).

## [0.1.0] - 2026-11-05

First public release. Earlier builds were published as 1.0.0–1.0.2 and have been withdrawn: E7 has been
tested and hardened but has not yet run in production, so it is versioned 0.x until it has.

### Language and VM
- **Language**: E7, a strict subset of TypeScript (every E7 program passes `tsc --strict` and prints the
  same output on Node.js): classes, interfaces, enums, tuples, closures, destructuring, spread, `?.`, `??`,
  template strings, multi-file modules, `Map`/`Set`, `Uint8Array`, `bigint`, `Date`, type-checked
  `JSON.parse(text) as T`, fixed-width integers `i8`…`u64`, and `debugger;`.
- **e7vm**: register-based bytecode VM in C++17 with a bytecode verifier, precise GC, inline caches,
  instruction budget (including native calls), memory and call-depth limits, suspend/resume.
- **Crypto and encodings** (`e7:crypto`, `e7:encoding`): SHA-1/2/3, MD5, HMAC, HKDF, PBKDF2, AES-GCM/CBC/CTR,
  ChaCha20-Poly1305, Ed25519, X25519, ECDSA P-256/384/521, RSA PKCS#1 v1.5 and PSS; hex, base64(url),
  base32, UTF-8, PEM. `KeyHandle` keeps keys in the host.
- **`e7:json`**: `tryParse(text) as T | null` parses and validates like `JSON.parse(text) as T` but returns
  `null` for malformed or mismatching input instead of terminating; `lastError()` gives the reason.
- **Optional Ed25519 module signatures** (`e7c --sign`, `trusted_keys` / `require_signature`).
- **Diagnostics**: "did you mean ...?" suggestions, messages naming the alternative for unsupported TypeScript
  features (`any`, generics, `try`/`catch`, `async`, `for...in`, regular expressions, accessors), source line
  with a caret; stack traces with file names across modules, native frames, columns when debug information is
  present, and long recursion collapsed into one line.

### Tools
- `e7` (run, `e7 test`, `e7 debug`) and `e7c` (compile, sign, disassemble); prebuilt binaries for Linux
  x64/arm64, macOS universal and Windows x64; Homebrew formula and Scoop manifest.
- **Test runner** `e7 test` and the `e7:test` module (`assert`, `assertEqual`, `assertNotEqual`, `assertClose`,
  `assertThrows`, `fail`): every test runs in a fresh VM with its own budget and memory limit; `--filter`, `--json`.
- **Debugger** ([docs/DEBUGGING.md](docs/DEBUGGING.md)): optional debug information (`e7c --debug`), a C debug API
  (`e7_debug_*`: breakpoints, stepping, pause, call stack, variables, evaluation), the `e7 debug` command-line
  debugger, and a WebAssembly debug session API.
- **VS Code extension "E7 Language"**: diagnostics as you type, *Run File*, *Compile to Bytecode*, *Show
  Bytecode*, and debugging (F5) on the bundled WebAssembly VM.
- **E7 skill for AI assistants** (`skills/e7`), checked against the compiler in CI. Claude Code:
  `/plugin marketplace add e7lang/e7`, then `/plugin install e7@e7lang`; other tools: `e7-skill.zip`.
- **Website** with docs and an in-browser playground (WebAssembly): https://e7lang.github.io

### Embedding and packages
- C API; Kotlin/Java (JNI), Swift and C# bindings; Android, iOS, Windows, macOS and Linux builds.
- Swift Package Manager (`.package(url: "https://github.com/e7lang/e7.git", from: "0.1.0")`, product `E7VM`),
  Android AAR `org.e7vm:e7vm-android` and JVM jar `org.e7vm:e7vm`, NuGet package `E7VM`, npm package
  `@e7lang/e7` (compiler, VM and debug session as WebAssembly for Node.js 18+ and browsers).

### Verification
- Differential tests against Node.js 22, `tsc --strict` checks, C API, debugger and locale tests, binding tests
  on an Android emulator, the iOS simulator, JVMs and .NET; libFuzzer targets for the loader and compiler with
  ClusterFuzzLite (ASan + UBSan) on pull requests and nightly. See [docs/PRODUCTION.md](docs/PRODUCTION.md).

[Unreleased]: https://github.com/e7lang/e7/compare/v0.2.7...HEAD
[0.2.7]: https://github.com/e7lang/e7/compare/v0.2.6...v0.2.7
[0.2.6]: https://github.com/e7lang/e7/compare/v0.2.5...v0.2.6
[0.2.5]: https://github.com/e7lang/e7/compare/v0.2.1...v0.2.5
[0.2.1]: https://github.com/e7lang/e7/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/e7lang/e7/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/e7lang/e7/releases/tag/v0.1.0
