# Roadmap

## 0.2.0: ready for a production pilot

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

## Later

- Production pilot (shadow run, then a small rollout); call E7 production-ready only after a pilot and a
  third-party audit.
- Incremental GC if pilot data shows pauses that matter on low-end devices.
- `finally`.
