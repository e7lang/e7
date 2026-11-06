# Roadmap

## 0.2.0: ready for a production pilot (released 2026-11-05)

Everything below shipped in 0.2.0 (see [CHANGELOG.md](../CHANGELOG.md)) and 0.2.1 (the debugger option to stop on
caught errors, and the fixes of the try/catch security review), with two adjustments: `URIError` is
catchable too, and detached Ed25519 signatures (`e7_sign_detached` / `e7_verify_detached`, `e7c --sign-file`) were
added for update manifests.

Goal: a release an app can pilot in production. Every change to the bytecode format or the C ABI that we can
foresee lands in 0.2, so that from 0.2.x on a minor release line stays compatible. 0.2 does **not** keep
compatibility with 0.1: `.e7b` files must be recompiled.

### M1. Format and interfaces (breaking)

**1.1 Bytecode format v2 and version targeting**
- Header: `version = 2`, followed after `flags` by `requires`, the minimum e7vm version that can run the module.
- Every opcode and native function has a "since" version (`src/core/since.h`); the compiler computes `requires`
  from what a module actually uses. `e7c --target 0.2` rejects features a 0.2 VM does not have.
- `e7c --info file.e7b` prints the format, `requires`, signature key id and exports.
- A VM rejects an unknown format or a module that requires a newer VM with `E7_ERR_VERSION`.
- Compatibility corpus: `tests/compat/0.1.0/` (must be rejected with `E7_ERR_VERSION`) and, from the 0.2.0
  release on, a frozen `tests/compat/0.2.0/` that every 0.2.x must run with unchanged output.

**1.2 C ABI**
- `e7_config` and `e7_compile_options` start with `struct_size`; the `*_init` functions fill it and the VM reads
  only the part it knows.
- `E7_ERR_VERSION`; `e7_inspect()` reads a module's format, `requires` and signature without loading it.
- `e7_gc_stats()`: collections, total and maximum pause, heap size and peak; `gc_growth_percent` in `e7_config`.

**1.3 try/catch**
- `try { } catch (e) { }` and `catch { }`. `finally` stays a compile error until 0.3.
- `throw` takes `new Error / TypeError / RangeError(message)`; `throw "text"` is removed.
- The catch variable (`unknown` in TypeScript) can be narrowed with `e instanceof Error` (then `.message`,
  `.name`), or passed to `String(e)` / `console.log(e)`.
- Only errors that Node.js also throws are catchable: `throw`, `JSON.parse` syntax errors (SyntaxError), range
  errors such as BigInt division by zero (RangeError), `!` on null (TypeError). E7-specific checks (array index
  out of range, `as T` shape mismatch, integer conversion) and budget / memory / stack exhaustion still terminate
  the script, so an E7 program keeps printing the same output as on Node.js.
- Bytecode: a per-function handler table (range, handler, register), checked by the verifier. The debugger
  distinguishes uncaught (default) and all runtime errors.

### M2. Pilot support

- **`E7ModuleManager`** for Kotlin and Swift: signed manifest (version, url, sha256, rollout percentage, kill
  switch), download and verification, `selfTest()` before activation, last-known-good rollback after repeated
  failures, bundled fallback, error reporting callback. `tools/manifest.py` builds and signs manifests.
- **`docs/DEPLOYMENT.md`**: key management and rotation, rollout, kill switch, rollback, `--target`, sizing the
  budget and memory limit from measurements, error reporting, shadow runs against the native implementation.

### M3. Quality gates

- `e7 --stats`: instruction ticks, GC collections, maximum pause, peak heap.
- Performance gate in CI: ticks and peak heap per benchmark compared exactly with `bench/baseline.json`, and the
  interpreter's machine instruction count (cachegrind) within 3 % of the baseline.
- GC pause benchmark in `bench/`.
- `fuzz_natives`: a fuzzer that calls crypto / encoding natives with arguments generated from their signatures.
- Releases carry an SPDX SBOM and GitHub build provenance attestations.

### Not in 0.2

Incremental GC (decided from pilot measurements), `finally`, OSS-Fuzz (after a pilot), a third-party security
audit, and code-signing certificates / registry accounts (owner actions).

## 0.2.5: stability, reliability and TypeScript compatibility (released 2026-11-06)

Everything below shipped (see CHANGELOG.md). Beyond the plan: Math bit-identical to Node.js (fdlibm), eight compiler
issues and ten library differences found by the differential fuzzer, and the bugs the stability checks found.

A compatible 0.2.x release: modules from 0.2.0 / 0.2.1 keep running, and modules the 0.2.5 compiler builds without
new library functions still run on 0.2.0 / 0.2.1 VMs (`requires` stays 0.2.0). New natives are marked "since 0.2.5"
(`src/core/since.h`), so an older VM rejects a module that needs them with `E7_ERR_VERSION`.

A probe of 59 common TypeScript patterns before this release: 26 compiled and printed the same as Node.js, 33 were
rejected (about 17 of them are compiler-only work, listed under P1).

### P0. Stability and reliability
1. GC stress mode (`E7_GC_STRESS`): a collection at every safepoint and allocation; the test suite runs under it
   with ASan in CI.
2. Allocation-failure injection: fail the N-th allocation for every N; each run must end cleanly with
   `E7_ERR_MEMORY` and the heap count must return to its baseline.
3. Equivalence runs: the whole suite with a budget slice of 1 (suspend/resume at every check), with a debugger
   stepping through everything, and with switch dispatch; output identical to the normal run.
4. Soak test: one VM, 100,000 calls; memory and GC statistics stay flat.
5. Threads: several VMs on several threads under TSan.
6. Forward compatibility in CI: modules compiled by the new compiler run on a VM built from the previous release tag.
7. Coverage report (llvm-cov) and tests for uncovered natives and error paths.
8. Known differences: Map insertion during iteration after many deletions; stale rows in SPEC section 9.

### P1. TypeScript compatibility (compiler only)
- Default parameters in arrow functions, defaults in destructuring, rest elements in array and object
  destructuring, `a?.[i]`, spreading tuples into calls, `Array.from({ length: n }, fn)`.
- Classes: `abstract`, `#private` fields and methods, `static {}` blocks, getters and setters.
- `finally` (runs on normal completion, `return` / `break` / `continue` and catchable errors).
- `flatMap` / `flat`, `Object.entries` / `values` / `assign`, `replace` / `replaceAll` with a callback.
- Method shorthand in object literals.

### P2. Further
- Differential fuzzing: random programs in the E7 subset, run on e7 and Node.js, outputs compared (nightly).
- `console.log` of long arrays and nested objects formatted like Node.js (multi-line).
- Generics: generic functions, classes, interfaces and type aliases, compiled by monomorphization (no VM change).

## 0.2.6: the subset defined

- SPEC section 0.1: what defines E7 (tsc 5.6 with `strict` and `exactOptionalPropertyTypes` as the upper bound,
  ECMAScript as implemented by Node.js 22 for run-time behaviour, and the selection rules), with **types known at
  compile time** as the fixed rule that is never relaxed.
- TYPESCRIPT.md: every TypeScript feature with its status, reason and alternative, generated from sample programs
  that CI checks against the compiler and Node.js.
- Optional properties follow tsc's `exactOptionalPropertyTypes`: `x?: T` is left out, not set to `undefined`.
  This narrows the `Object.keys` / `console.log` difference for properties holding `undefined` (SPEC section 9)
  to properties whose declared type includes `undefined`.
- Fixes from an independent security review of the 0.2.5 VM changes.
- `scripts/macos.sh release` runs the full macOS test first, so a release cannot skip it.
- The compiler gaps the table found: `readonly` checked as in tsc, optional interface methods, recursive local
  functions and named function expressions, defaults that refer to earlier parameters, exhaustive enum switches,
  narrowing after an assignment in an `if` branch, clearer errors for unsupported features.

## 0.3: the language complete for its domain (done)

**Goal.** Everything E7's target scripts (signing and verification, protocol parsing, business rules) write in
everyday TypeScript compiles and behaves as on Node.js, including iteration, `async` / `await` against host
operations and regular expressions. After 0.3 the remaining gaps in docs/TYPESCRIPT.md are only features that
break the rules of SPEC section 0.1, or have a written reason to wait. Together with a production pilot started
during 0.3, that is the beta stage; 1.0 then needs pilot data, a third-party audit and a format freeze (see *Later*).

**Measurable targets** (checked by CI, docs/TYPESCRIPT.md is generated from the samples):

| Measure | 0.2.6 | 0.3 target | 0.3 result |
|---|---|---|---|
| Features supported / partial / unsupported (of 250) | 117 / 21 / 112 | about 150 / 20 / 80 | 153 / 18 / 79 |
| Unsupported rows whose reason is "not implemented yet" | 73 | at most 25, each with a decision | 15 (async generators, object literal accessors and the "waiting with a reason" list below) |
| Differences from Node.js in SPEC section 9 | 16 rows | the `undefined`-property row removed; every remaining row is an E7 check, an E7 extension or a stated output detail | the `undefined` row removed; new rows state generator, async and regular expression details |
| Production pilot | none | running in shadow mode | the release makes it possible; it runs in an app (see *Later*) |

**What shipped** (each item below; details in CHANGELOG.md 0.3.0 and DESIGN-0.3.md's implementation notes):
generators and iterators (A), `async` / `await` with host tickets in the C API and every binding (B), regular
expressions (C), the compiler gaps of D except object literal accessors, the standard library of E, properties
that hold `undefined` (F), the quality gates of G (the seventh and eighth security reviews in PRODUCTION.md), and
the C# `E7ModuleManager` (H). Not shipped: async generators and object literal accessors (rarely needed by the
target scripts; they wait for a later release).

Candidates must satisfy the rules of SPEC section 0.1; features that need run-time type information (`any`,
`unknown`, general unions, index signatures, `keyof` / conditional / mapped types) stay out.

### Compatibility policy for 0.3

- **0.2 modules keep running**: a 0.3 VM loads format-2 modules unchanged (new CI job: the 0.2.6 compiler's output for
  the whole suite runs on the 0.3 VM with identical output).
- **Format 3 only when needed**: the compiler emits format 2 for a module that uses no 0.3 feature, so 0.2.x VMs keep
  running it (the existing forward-compatibility job, against v0.2.6). A module that uses generators, `async`, regular
  expressions or the new object layout is format 3 with `requires 0.3.0`; a 0.2.x VM rejects it with
  `E7_ERR_VERSION` at load time. `e7c --target 0.2` refuses those features with a compile error.
- **Intended source-level changes**, each listed in CHANGELOG with what to do: literal types are checked (D5; tsc
  checks them too), and `Object.keys` / spread / `console.log` show a property set to `undefined` (F; as Node.js does).
- The C API only grows (the `struct_size` scheme): existing hosts recompile without changes.

### Design decisions first (written: [DESIGN-0.3.md](DESIGN-0.3.md), format 3 in [BYTECODE.md](BYTECODE.md) section 6)

- **D1. Coroutines.** A generator or `async` activation owns its frames and registers in a heap object; `yield` /
  `await` move them off the VM stack, resuming moves them back. Budget suspension keeps working inside them. A native
  cannot be suspended halfway, so `yield` / `await` inside a callback that a native calls (a `sort` comparator) is a
  compile error. The verifier checks the new opcodes like the existing ones (register kinds at every resume point).
- **D2. `async` without an event loop.** The VM implements ECMAScript promise jobs (a microtask queue with the
  spec's order, including the one-tick `await`), so ordering equals Node.js. The host is the event loop: a host
  function declared `declare function fetchKey(id: string): Promise<Uint8Array>` gets a ticket when called; `e7_call`
  returns `E7_PENDING` when no microtask is left and a ticket is open; the host settles it
  (`e7_settle(vm, ticket, value | error)`) and resumes. In scope: `async` functions, methods and arrows, `await`,
  `Promise<T>`, `then` / `catch` / `finally`, `new Promise((resolve, reject) => ...)`, `Promise.all` / `allSettled` /
  `race` / `any` over one element type, top-level `await` in the entry module, async iteration (`for await`).
  Not in scope: timers (`setTimeout`); a host that needs one provides it as an async host function.
- **D3. Format 3.** New opcodes for coroutines, function and class flags, a regular expression table; written
  only for modules that use them (format 2 otherwise), with `since 0.3.0` in since.h.
- **D4. Regular expressions.** An own backtracking engine with ECMAScript semantics (flags `dgimsuy`; the `v` flag
  later), compiled at compile time for literals (a syntax error in a literal is a compile error) and at run time for
  `new RegExp(source)`. Every step is charged to the instruction budget and backtracking memory to the memory limit,
  so a catastrophic pattern ends with `E7_ERR_BUDGET` instead of hanging (ReDoS is bounded by the host's limits). Named
  groups of a literal are known at compile time (`m.groups?.year` is a `string | undefined`); for a pattern built at
  run time only indexed groups are available. Unicode property escapes (`\p{...}`) and case folding use generated
  tables; their size is measured and kept optional if it is large.
- **D5. Literal types.** String, numeric and boolean literal types and unions of literals of one base type
  (`"GET" | "POST"`, `0 | 1`) are checked at compile time and represented as their base type, which the fixed rule
  allows. Unions of object types (discriminated unions) stay out.
- **D6. Recursive interfaces and type aliases** (`interface Tree { children: Tree[] }`): named object types are
  resolved lazily so they can refer to themselves.

- **D7. Properties that hold `undefined`** (item F): a presence mask in records whose type has optional or
  `undefined`-typed properties; reads stay a single load.

### A. Generators and iterators (VM: D1)
`function*`, `yield`, `yield*`, generator methods; `Generator<T>` / `Iterator<T>` / `IterableIterator<T>`; classes
with a `[Symbol.iterator]()` method (the one well-known symbol, as a fixed method name) in `for...of`, spread and
destructuring; `return()` on early exit as in the spec. Debugger: stepping into and out of generators.

### B. `async` / `await` (VM: D1, D2)
As in D2, with stack traces across `await`, debugger stepping over `await`, and host bindings: Kotlin (`suspend`),
Swift (`async`), C# (`Task`), JavaScript / WebAssembly (`Promise`). The `e7` CLI and the Node.js shim provide a few
test-only async host functions, so the differential tests compare ordering with Node.js.

### C. Regular expressions (VM: D4)
Literals and `RegExp`; `test`, `exec`, `lastIndex`, `match`, `matchAll`, `replace` / `replaceAll` (with `$1`, `$<name>`
and callbacks), `split`, `search`. Tested against the test262 RegExp tests and by a fuzzer that compares random
patterns and inputs with V8.

### D. Language gaps (compiler only) — mostly shipped in 0.2.7

Shipped in 0.2.7: recursive types, destructuring assignment (object patterns, defaults, rest), `||=` / `&&=`, the
comma and `void` operators, enum constant expressions, private accessors, narrowing by conditional expressions and
of static field paths, default exports and imports, re-exports, side-effect imports, classes / types / enums through
namespace imports. Still open for 0.3: literal types (D5), `never` in the `default` of an exhaustive switch, getters
and setters in object literals, computed property names, built-in functions and functions with defaults as values,
overrides with compatible signatures, `declare` fields, tagged templates and `String.raw`, type predicates and
assertion functions, `JSON.parse` into recursive types (needs D6's descriptor back-references).

Original list:
Recursive types (D6); literal types (D5); object destructuring assignment, with defaults and rest in assignment
patterns; `||=` / `&&=`; getters and setters in object literals; computed property names with keys known at compile
time (string constants, enum members); `never` in the `default` of an exhaustive switch, narrowing by a conditional
assignment, narrowing of static field paths; classes, types and enums through `import * as ns`; default exports and
imports, re-exports, side-effect imports; the comma and `void` operators; tagged templates and `String.raw`; function
declarations with defaults and built-in functions as values (`xs.map(Math.sqrt)`); overrides with a compatible
signature (fewer parameters, covariant return type); private accessors (`get #x()`); `declare` fields; constant
expressions in enum members; type predicates and assertion functions for nullability and class types (checked like
`as`). These need no VM change, so they can ship early in 0.2.x patch releases.

### E. Standard library
The ES2023 copying methods (`toSorted`, `toReversed`, `toSpliced`, `with`); `Date` setters; `Error` `cause` and
`AggregateError` (with `Promise.any`); typed arrays other than `Uint8Array` (`Int8Array` ... `Float64Array`,
`BigInt64Array`); `structuredClone` of statically typed values.

### F. Properties that hold `undefined`
Objects of interface / object types tell a missing property from one holding `undefined` (D7's presence mask), so
`Object.keys` / `entries` / `values`, spread and `console.log` match Node.js for `x: T | undefined` and
`x?: T | undefined`. This removes the `undefined` row from SPEC section 9.

### G. Quality gates (as for 0.2.5, extended)
- The differential fuzzer generates generators, `async` code with settled host tickets in random order, regular
  expressions and literal types; no open mismatch at release.
- GC stress, allocation-failure injection and the budget-slice-1 / debugger-step equivalence runs cover suspended
  generators and pending promises.
- Malicious-bytecode corpus and verifier tests for every new opcode; fuzzing of the format-3 loader.
- ReDoS tests: catastrophic patterns end with `E7_ERR_BUDGET` within the configured budget.
- Backward (0.2.6 modules on 0.3) and forward (format-2 output on v0.2.6) compatibility jobs.
- An independent security review of all VM changes (coroutines, promises, the regular expression engine) before the
  release, recorded in PRODUCTION.md.
- Benchmarks: no slowdown above 5% on the existing suite.

### H. Pilot and ecosystem
- Production pilot: one app runs E7 scripts in shadow mode (E7 computes, the existing code decides, results are
  compared) during 0.3, then a small rollout.
- A C# `E7ModuleManager` (Kotlin/Java and Swift have one).
- SPEC, TYPESCRIPT.md, the language tour, the playground and the AI skill updated with each feature.

### Order
1. D1-D7 written down (done: DESIGN-0.3.md); format 3 in BYTECODE.md section 6 (done).
2. D (compiler only), released early as 0.2.x patches where it changes nothing for existing programs.
3. A, then B (they share D1).
4. C, independent of A and B, can run in parallel.
5. E and F.
6. G throughout, the security review and the release last.

### Not in 0.3
- Excluded by the rules of SPEC section 0.1 (about 60 rows of TYPESCRIPT.md): `any`, `unknown` as a declared type,
  unions other than with `null` / `undefined` (including discriminated unions of object types), index signatures and
  `Record`, `keyof` / `typeof` types, conditional, mapped and template literal types, intersection types, `Symbol`
  values, `Proxy` / `Reflect`, `eval`.
- Waiting with a reason: `Intl` and locale formatting (locale data differs between platforms), `normalize` (Unicode
  tables), `WeakMap` / `WeakSet` (ephemeron tables in the collector), class expressions and mixins (classes are not
  values), namespaces, decorators, JSX, `var`, non-ASCII identifiers, timers.

## Later

- **1.0** when: a production pilot has run for a while with its data published in PRODUCTION.md, a third-party
  security audit is done, the bytecode format and the C API are frozen (1.x VMs run every 1.x module), and
  performance on low-end target devices is measured.
- Incremental GC if pilot data shows pauses that matter on low-end devices.
