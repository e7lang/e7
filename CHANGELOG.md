# Changelog

All notable changes to E7 / e7vm are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

**Versioning while E7 is 0.x**: a minor release (0.1 → 0.2) may contain incompatible changes to the language,
the C API or the bytecode format; each one is listed under *Changed* or *Removed* with what to do. A patch
release (0.1.0 → 0.1.1) is compatible. A module's header names the oldest e7vm that can run it (`requires`), and a
VM rejects a module it cannot run at load time rather than running it incorrectly.

## [Unreleased]

## [0.1.0] - 2026-11-06

The first release of E7, a strict subset of TypeScript compiled to verifiable bytecode, and e7vm, the C++17
virtual machine that runs it inside apps on Android, iOS, macOS, Windows, Linux and WebAssembly. Scripts are
delivered from a server, signed, verified at load time and run in a sandbox.

### Language ([docs/SPEC.md](docs/SPEC.md), [docs/TYPESCRIPT.md](docs/TYPESCRIPT.md))
- Every E7 program is a valid TypeScript program (checked with `tsc --strict` and `exactOptionalPropertyTypes`)
  and prints what Node.js 22 prints; every value's type is known at compile time.
- Types: `number`, `boolean`, `string`, `bigint`, fixed-width integers (`i8` ... `u64`, `int`) with machine
  semantics, arrays, tuples, `Map` / `Set`, `Uint8Array` and the other typed arrays, `DataView`, `Date`,
  `Promise`, generators and iterators, nullable types (`T | null`, `T | undefined`), literal types and unions of
  literals, enums, function types, recursive interfaces and type aliases.
- Classes (single inheritance, `abstract`, `implements`, static members and blocks, parameter properties,
  `#private` fields, getters and setters), interfaces, object literals with methods and accessors, generics
  (compiled by monomorphization), closures, destructuring, spread, optional chaining, `??`, template literals and
  tagged templates, labeled statements, modules (`import` / `export`).
- `try` / `catch` / `finally` for the errors Node.js also throws; E7-specific checks and resource limits end the
  call.
- Generators, `async` / `await`, async generators and `for await`, with the promise job order of the
  specification; the host is the event loop (`declare function f(): Promise<T>` host functions).
- Properties that hold `undefined` behave as in JavaScript (a presence mask in records).
- No regular expressions: literals, `RegExp`, `match`, `matchAll` and `search` are compile errors that point to the
  string methods; leaving the engine out keeps the runtime about 160 KB smaller ([docs/DESIGN.md](docs/DESIGN.md) D4).
- 250 TypeScript features documented with a sample program each: 154 supported, 16 partial, 80 unsupported (with
  the reason and the alternative).

### Standard library
- Strings, arrays (including the ES2023 copying methods), `Map` / `Set`, `Math` (V8's fdlibm, bit-identical to
  Node.js 22 on x64), `JSON` (typed `JSON.parse(text) as T`, also into recursive types), `Date` (UTC), `BigInt`,
  typed arrays, `structuredClone`, `Error` with `cause` and `AggregateError`, `console.log` formatted as Node.js does.
- `e7:crypto`, modern algorithms only: SHA-256/384/512, HMAC, HKDF, PBKDF2, AES-GCM, ChaCha20-Poly1305, Ed25519,
  X25519, secure random, `timingSafeEqual`; keys can stay in the host as `KeyHandle`s (BearSSL and Monocypher
  underneath). No MD5, SHA-1, SHA-3, AES-CBC/CTR, RSA or ECDSA: a host function covers a legacy protocol.
- `e7:encoding` (hex, Base64, Base64url, Base32, PEM, UTF-8, CRC-32), `e7:json`, and `e7:test` for unit tests run by
  `e7 test`.

### e7vm ([docs/BYTECODE.md](docs/BYTECODE.md), [docs/DESIGN.md](docs/DESIGN.md))
- Register VM with untagged 8-byte registers, typed 64-bit instructions, inline caches and a precise mark-sweep
  collector; frames on a VM-managed stack.
- Bytecode format 3 (`.e7b`) with a `requires` header field (the oldest e7vm that can run the module); a VM
  rejects another format or a newer `requires` with `E7_ERR_VERSION`.
- A load-time verifier for every function (register kinds, indices, jump targets, call signatures, handler tables,
  coroutine and record flags, debug information).
- Sandbox: no ambient capabilities, instruction budget with suspend / resume, memory, stack and frame limits, and
  Ed25519 module signatures with trusted keys.
- Debugger: breakpoints, stepping, scopes and variables, break on caught / uncaught errors, through the C API and a
  JSON protocol (DAP in the VS Code extension).

### C API and bindings ([docs/EMBEDDING.md](docs/EMBEDDING.md), [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md))
- C API (`include/e7/e7.h`): load, call, resume, host functions (synchronous and async with tickets), KeyHandles,
  budgets, limits, module inspection, signing and detached signatures, GC statistics, debugging.
- Bindings: Swift (SwiftPM, XCFramework), Kotlin / Java (Android AAR, JVM jar), C# (.NET, NuGet) and JavaScript
  (WebAssembly package `@e7lang/e7`); module managers for Swift, Kotlin / Java and C# with signed update manifests,
  staged rollout, kill switch and rollback.

### Tooling
- `e7c` (compile, check, sign, inspect, disassemble, key generation, detached signatures), `e7` (run, test, debug),
  `tools/manifest.py` for update manifests.
- VS Code extension (diagnostics, debugging), a browser playground, `lib/e7.d.ts` for editors and `tsc`, and an AI
  skill (`skills/e7`).

### Verification ([docs/PRODUCTION.md](docs/PRODUCTION.md))
- Differential tests against Node.js 22, `tsc --strict` checks, a differential fuzzer of random programs, GC-stress,
  allocation-failure, equivalence (budget slice 1, single-stepping debugger), soak and ThreadSanitizer runs, C API,
  debugger and binding tests on an Android emulator, the iOS simulator, JVMs and .NET; libFuzzer targets for the
  loader, compiler and natives (ClusterFuzzLite on pull requests and nightly).
- Seven independent security reviews (bytecode loading, natives and the C API, crypto, release sign-off,
  `try` / `catch`, the GC and formatter, coroutines and promises), every finding fixed with a regression test.

[Unreleased]: https://github.com/e7lang/e7/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/e7lang/e7/releases/tag/v0.1.0
