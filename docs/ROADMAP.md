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
- Fixes from an independent security review of the 0.2.5 VM changes.

## 0.3 (planned)

Candidates must satisfy the rules of SPEC section 0.1; features that need run-time type information (`any`,
`unknown`, general unions, index signatures, `keyof` / conditional / mapped types) stay out.

- Generators and iterators, `async` / `await` (coroutines in the VM).
- Pilot feedback: whatever the first production pilot needs.
- A C# `E7ModuleManager` (Kotlin/Java and Swift have one).

## Later

- Production pilot (shadow run, then a small rollout); call E7 production-ready only after a pilot and a
  third-party audit.
- Incremental GC if pilot data shows pauses that matter on low-end devices.
