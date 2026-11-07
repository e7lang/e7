<img src="docs/logo.svg" width="72" alt="E7 logo" align="right">

# e7vm — the E7 language and virtual machine

[![CI](https://github.com/e7lang/e7/actions/workflows/ci.yml/badge.svg)](https://github.com/e7lang/e7/actions/workflows/ci.yml) [![Release](https://img.shields.io/github/v/release/e7lang/e7)](https://github.com/e7lang/e7/releases/latest) [![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE) [![Playground](https://img.shields.io/badge/try-playground-ff6a2b)](https://e7lang.github.io/playground)

**English** | [简体中文](README.zh-CN.md) · Website: [e7lang.github.io](https://e7lang.github.io) · [**Try it in your browser**](https://e7lang.github.io/playground) · [Download](https://github.com/e7lang/e7/releases/latest)

**E7** is a **strictly, statically typed subset of TypeScript**: every valid E7 program is also a valid
TypeScript program. **e7vm** is a small embeddable bytecode VM written in C++17 for
**Android, iOS, Windows, macOS and Linux**. It runs **server-delivered scripts** inside your app, mainly
**request signing and signature verification**, plus general math and string algorithms. Scripts are
sandboxed and fast; modules can optionally be signed.

```ts
// sign.ts: full type checking and completion in VS Code (via lib/e7.d.ts)
// Your app calls this. Ship updates from your server, without an app release.
import { hmac } from "e7:crypto";
import { hexEncode } from "e7:encoding";

export function sign(msg: string, key: string): string {
  const enc = new TextEncoder();
  const mac = hmac("sha256", enc.encode(key), enc.encode(msg));
  return hexEncode(mac);
}
```

> The name: `everettjf` = **e** + `verettj` (7 letters) + **f** → **e7f**, so the language is **E7**.

## Why e7vm

- **Ship algorithm updates without an app release.** Compile on your server, deliver `.e7b` bytecode, and the app verifies and runs it.
- **Safe by construction.**
  - Optional Ed25519 module signatures, for when scripts travel over a network you don't control.
  - A bytecode verifier that guarantees memory safety even for tampered bytecode.
  - An instruction budget, memory limit and call-depth limit.
  - No JIT, which App Store rules require.
  - Scripts can only call the host functions you register.
- **Secrets stay in the host.** A `KeyHandle` lets scripts use a key held in the Keychain or Keystore without ever seeing the key bytes.
- **TypeScript you already know.** The same JS semantics, checked by `tsc --strict`, and differential-tested against Node.js.
- **Small and fast.**
  - About 1.2 MB for the runtime only (about 460 KB compressed), or about 2.7 MB with the on-device compiler
    (Android arm64, crypto included).
  - Interpreter speed comparable to or faster than V8's interpreter (`--jitless`).
  - Standard crypto runs as native code.

## Architecture

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/architecture-dark.png">
    <img src="docs/architecture-light.png" alt="e7vm architecture" width="780">
  </picture>
</p>

| Layer | Where | What it does |
|---|---|---|
| Compiler | `src/compiler/` | Lexer and parser, strict type checker (narrowing, closures, classes, modules), register-allocating code generator |
| Bytecode | `src/core/` | `.e7b` format with CRC32, optional Ed25519 signature trailer, verifier, disassembler |
| VM | `src/vm/` | Register-based interpreter with 64-bit instructions, typed registers, inline caches, precise mark-sweep GC, budget/suspend |
| Standard library | `src/stdlib/`, `src/crypto/` | JS-compatible built-ins; crypto and encoding run as native code |
| Embedding | `src/capi/`, `platform/` | C API; Java/Kotlin (JNI), Swift, and C# bindings |

## Quick start

### 1. Install or build

**Prebuilt binaries:** download `e7-<version>-<platform>` for Linux (x64, arm64), macOS (universal) or
Windows (x64) from the [latest release](https://github.com/e7lang/e7/releases/latest). Each archive has
`bin/e7`, `bin/e7c`, `include/e7/e7.h`, static libraries in `lib/`, `types/e7.d.ts` and examples.
Every release file and package (archives, `.nupkg`, `.aar`, `.jar`, `.tgz`, `.vsix`, skill zip) has a signed
GitHub build provenance attestation: `gh attestation verify e7-<version>-<platform>.tar.gz --repo e7lang/e7`
checks that it was built by this repository's release workflow from the tagged commit. An SPDX SBOM
(`e7-<version>.spdx.json`) is attached to each release.
**.NET:** `dotnet add package E7VM` (C# binding with native libraries for Windows, Linux and macOS).
No install at all: the [playground](https://e7lang.github.io/playground) runs the compiler and VM in your browser (WebAssembly).
**Node.js / browsers:** [packages/e7](packages/e7) is the same compiler and VM on WebAssembly, with a JavaScript
API (`compile`, `check`, `run`, `disassemble`) and the `e7-js` command. It is not published to the npm registry
(E7 targets native apps); download [`e7lang-e7-0.1.0.tgz`](https://github.com/e7lang/e7/releases/download/v0.1.0/e7lang-e7-0.1.0.tgz) from the release and run
`npm install ./e7lang-e7-0.1.0.tgz`.

**From source:**

```bash
git clone https://github.com/e7lang/e7.git && cd e7
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Prerequisites: CMake ≥ 3.16 and a C++17 compiler (GCC, Clang, MSVC); Ninja is optional.
This produces `build/e7` (run scripts), `build/e7c` (compile, disassemble, optionally sign) and `build/libe7vm_static.a`.

### 2. Write and run a script

[`examples/embed/sign.ts`](examples/embed/sign.ts):

```ts
import { hmac } from "e7:crypto";
import { hexEncode } from "e7:encoding";

// Called by the host app: returns hex(HMAC-SHA256(secret, message))
export function sign(msg: string, key: string): string {
  const enc = new TextEncoder();
  return hexEncode(hmac("sha256", enc.encode(secret), enc.encode(message)));
}

export function main(): string {
  return sign("amount=100&currency=CNY", "s3cr3t");
}
```

```bash
cd examples/embed
../../build/e7 sign.ts
# 0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6
```

`e7` compiles the file in memory and calls the exported `main` (or the function named by `--call`).

### 3. Compile to bytecode

```bash
../../build/e7c sign.ts -o sign.e7b               # portable bytecode: same file on every platform
../../build/e7 sign.e7b                           # run it
../../build/e7c --disasm sign.ts                  # optional: inspect the bytecode
```

A `.e7b` file runs anywhere e7vm runs, so you can hand it to someone else or ship it to your app.

### 4. Optional: sign your modules

Signing is off by default and only matters when scripts travel over a channel you don't fully trust
(for example, downloaded by an app at runtime). It lets the receiver refuse anything that wasn't
produced by you:

```bash
../../build/e7c --keygen release                  # release.seed (keep secret) + release.pub
../../build/e7c sign.ts -o sign.e7b --sign release.seed
../../build/e7 --trust release.pub sign.e7b       # runs only if signed by this key
```

Without `--trust` (or trusted keys in the app), a signed module simply runs like an unsigned one.

### 5. Run it from your app

[`examples/embed/host.c`](examples/embed/host.c), trimmed:

```c
#include "e7/e7.h"

e7_config cfg;
e7_config_init(&cfg);
cfg.memory_limit = 32u << 20;
/* optional: only run modules signed by your key
   cfg.require_signature = 1; cfg.trusted_keys = pub; cfg.num_trusted_keys = 1; */
e7_vm* vm = e7_vm_new(&cfg);

e7_module* mod;
if (e7_load(vm, code, len, &mod) != E7_OK) { puts(e7_last_error(vm)); return 1; }

e7_value args[2];
args[0].type = E7_STRING; args[0].v.str.ptr = "amount=100&currency=CNY"; args[0].v.str.len = 23;
args[1].type = E7_STRING; args[1].v.str.ptr = "s3cr3t";                  args[1].v.str.len = 6;
e7_value ret;
e7_set_budget(vm, 10000000);          /* stop runaway scripts */
if (e7_call(vm, mod, "sign", args, 2, &ret) == E7_OK)
  printf("%.*s\n", (int)ret.v.str.len, ret.v.str.ptr);
e7_vm_free(vm);
```

```bash
cc -std=c11 host.c -I../../include ../../build/libe7vm_static.a \
   ../../build/libe7_bearssl.a ../../build/libe7_monocypher.a -lstdc++ -lm -o host
./host
# 0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6
```

Kotlin, Swift and C# use the same flow: create the VM (optionally with a trusted key), call `load(bytes)`,
then `call("sign", ...)`. In Swift, add the package with Swift Package Manager and call it directly:

```swift
// Package.swift: .package(url: "https://github.com/e7lang/e7.git", from: "0.1.0")
//                .product(name: "E7VM", package: "e7")
import E7VM

let vm = try E7VM()
let module = try vm.load(bytecode)  // contents of sign.e7b
let mac = try vm.call(module, "sign", .string("amount=100&currency=CNY"), .string("s3cr3t"))
// .string("0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6")
```

On Android, add the AAR from Maven:

```kotlin
dependencies {
    implementation("org.e7vm:e7vm-android:0.1.0")   // or org.e7vm:e7vm:0.1.0 for desktop JVMs
}
```

Until it is on Maven Central, add the GitHub Packages repository `https://maven.pkg.github.com/e7lang/e7`
(needs a token with `read:packages`; see [docs/EMBEDDING.md](docs/EMBEDDING.md#31-gradle-dependency-recommended))
or download the `.aar` from the [release](https://github.com/e7lang/e7/releases/latest). See [docs/EMBEDDING.md](docs/EMBEDDING.md) for each platform, plus host functions,
`KeyHandle`, budgets and time-slicing.

### 6. Editor setup

Add [`lib/e7.d.ts`](lib/e7.d.ts) to your `tsconfig.json` `files` to get completion and type errors in
VS Code. Running `tsc --strict` on your scripts gives the same diagnostics as `e7c`.

The **E7 Language** VS Code extension ([editors/vscode](editors/vscode), publisher `e7lang`; the `.vsix` is
attached to each [release](https://github.com/e7lang/e7/releases/latest)) adds the E7 compiler's own
diagnostics as you type, *E7: Run File*, *E7: Compile to Bytecode* and *E7: Show Bytecode*. It works on
`*.e7` files, `.ts` files that import `"e7:..."` modules and files matching the `e7.files` setting.

### 7. Debugging

`e7 debug script.ts` is a small command-line debugger (`b file:line`, `c`, `n`, `s`, `finish`, `bt`, `locals`,
`p name.field`, `list`); `debugger;` statements stop there. Hosts get the same through the C debug API
(`e7_debug_*`: breakpoints, stepping, pause, call stack, expandable variables) and JavaScript through the
WebAssembly build's JSON protocol (`createDebugSession()` in `@e7lang/e7`). In VS Code, the E7 extension debugs
E7 files with F5: breakpoints, stepping, variables, watch and hover. Compile with `e7c --debug` to keep
variable names and columns in `.e7b` files; runtime errors then report `at f (file.ts:12:5)`. See
[docs/DEBUGGING.md](docs/DEBUGGING.md).

## Using E7 with AI assistants

E7 looks like TypeScript, so AI assistants tend to write TypeScript that E7 rejects (`any`, union
types, index signatures...). The **E7 skill** ([`skills/e7`](skills/e7)) teaches them what E7 supports, how to
rewrite what it doesn't, the standard library, and to compile-check every script with `e7c --check`. In our tests
it raised correct results from 88% to 100% and cut the time per task by about 40%.

- **Claude Code**: `/plugin marketplace add e7lang/e7`, then `/plugin install e7@e7lang`
  (or, on Claude Code 2.1.275+, `/plugin install e7 --marketplace e7lang/e7`).
  Without the plugin system: copy `skills/e7` to `~/.claude/skills/e7` (or `.claude/skills/e7` in your project).
- **claude.ai / Claude API / other tools**: use `e7-skill.zip` from the [latest release](https://github.com/e7lang/e7/releases/latest).
- **Any assistant that reads the web**: point it at [e7lang.github.io/llms.txt](https://e7lang.github.io/llms.txt)
  or the full text at [llms-full.txt](https://e7lang.github.io/llms-full.txt).

Every code example in the skill is compiled and run in CI, so the skill cannot drift from the language.

## Testing your scripts

Put tests in `*.test.ts` files, export functions whose names start with `test`, and use the assertions from the
built-in `e7:test` module ([`examples/tests/`](examples/tests/)):

```ts
import { assert, assertEqual, assertThrows } from "e7:test";
import { totalOf, discountPercent } from "./pricing.ts";

export function testDiscountCodes(): void {
  assertEqual(discountPercent(" welcome10 "), 10);
  assertEqual(totalOf([{ sku: "pen", unitCents: 199, qty: 3 }], null).total, 597);
}

export function testUnknownCodeIsRejected(): void {
  assertThrows(() => { discountPercent("FREE"); }, "unknown discount code");
}
```

```bash
build/e7 test                    # every **/*.test.ts under the current directory
build/e7 test examples/tests --filter Discount
```

```
✓ examples/tests/pricing.test.ts › testDiscountCodesAreCaseInsensitive (0.2 ms)

1 passed, 0 failed (1 file, 2 ms)
```

- Each test runs in a **fresh VM** with its own instruction budget (`--budget`, default 10^9) and memory limit
  (`--mem`, default 128 MiB), so tests cannot affect each other and an endless loop fails instead of hanging.
- A test passes when it returns normally (or returns `true`, if it returns `boolean`). A failed assertion, `throw`,
  runtime error, or exhausted budget or memory fails it. The runner prints the message, the stack trace and the test's
  output.
- Assertions: `assert`, `assertEqual` / `assertNotEqual` (structural equality for any type: arrays, objects, Maps,
  `Uint8Array`s, ...), `assertClose`, `assertThrows`, `fail` ([SPEC §7.1](docs/SPEC.md#71-e7test-assertions)).
- `--json` prints machine-readable results; the exit code is 1 when a test fails.
- Test files are ordinary TypeScript: with `lib/e7.d.ts` they type-check under `tsc --strict`.

Compile errors point at the code and suggest fixes:

```
cart.ts:4:13: error: cannot find name 'totl'; did you mean 'total'?
 4 | console.log(totl + tax);
   |             ^
```

## Features

- **Language.**
  - Declarations and functions: `let`/`const`, functions, arrow functions and closures.
  - Classes: single inheritance, `super`, static members (inherited), parameter properties, getters and setters,
    `abstract`, `#private` fields and methods, `static {}` blocks.
  - Types: interfaces, type aliases, enums, tuples, and **generics** (functions, classes, interfaces, aliases;
    compiled per type argument).
  - Syntax: destructuring with defaults and rest, object spread, `?.` (short-circuiting chains), `??`/`??=`,
    template strings, `for...of`, `switch` and labels.
  - Errors: `try`/`catch`/`finally` for the errors Node.js also throws.
  - Generators, `async`/`await`, `Promise` (the host is the event loop; host functions may be async), async
    generators and `for await`.
  - Object literal getters and setters, literal types, recursive interfaces.
  - Multi-file `import`/`export`, and type narrowing.
- **Same output as Node.js.** Every test program is compared with Node.js byte for byte; `console.log` formats
  like Node's `util.inspect`, and Math functions are bit-identical (V8's fdlibm) on every platform.
- **Strict types.**
  - There is no `any`; nullable types use `T | null | undefined`.
  - All names and types are resolved at compile time.
- **Numbers like JavaScript.**
  - `number` is a 64-bit float.
  - Opt-in fixed-width integers `i8 … u64` wrap on overflow and run as real machine integers.
  - Single-instruction intrinsics such as `rotl32`, `clz32` and `mulhi64`.
- **Standard library.**
  - `Math`, `Number`, `String`, `Array` (`map`/`filter`/`reduce`/`sort` …), `Map`, `Set`, `bigint`, `Date` (UTC).
  - `JSON`: `JSON.parse(text) as T` **validates** the result against the type `T`.
  - Typed arrays (`Uint8Array` … `Float64Array`, `BigInt64Array`)/`ArrayBuffer`/`DataView`, `TextEncoder`/`TextDecoder`,
    `structuredClone`, `encodeURIComponent`.
- **`e7:crypto`.**
  - Hashes: MD5, SHA-1, SHA-2, SHA-3, Keccak.
  - Key derivation and MACs: HMAC, HKDF, PBKDF2.
  - Ciphers: AES-GCM/CBC/CTR, ChaCha20-Poly1305.
  - Signatures and key exchange: Ed25519, X25519, ECDSA P-256/384/521 (raw or strict DER), RSA PKCS#1 v1.5 and PSS.
  - Also CRC32, secure random bytes, and `KeyHandle`.
- **`e7:encoding`.** hex, base64, base64url, base32, UTF-8, PEM, plus byte concat and compare.
- **`e7:json`.** `tryParse(text) as T | null` validates like `JSON.parse` but returns `null` for malformed or
  mismatching (untrusted) input; `lastError()` says why, e.g. `expected a number at $.amount`.
- **Execution control.**
  - The instruction budget also covers heavy native calls.
  - Optional suspend/resume (`e7_resume`) for time-slicing on the UI thread.
- **Bindings.** C API ([`include/e7/e7.h`](include/e7/e7.h)), Java/Kotlin (JNI), Swift, C#, and
  JavaScript/WebAssembly ([`@e7lang/e7`](packages/e7)).

The full language reference is in [docs/SPEC.md](docs/SPEC.md). [docs/TYPESCRIPT.md](docs/TYPESCRIPT.md) lists
TypeScript feature by feature (the handbook chapters plus the JavaScript syntax and library a TypeScript program
uses) with what E7 supports, why the rest is left out and what to write instead; every row is a sample program
checked against the compiler, Node.js and `tsc`.

## Platforms

| Platform | Build | Bindings |
|---|---|---|
| Linux / macOS | `cmake -S . -B build && cmake --build build && ctest --test-dir build` | C/C++ |
| Android | Maven `org.e7vm:e7vm-android` (AAR), or `cd platform/android && ./gradlew :library:assembleRelease`, or `ANDROID_NDK_HOME=... platform/android/build_android.sh` | Java/Kotlin: `platform/java/org/e7vm` |
| JVM (desktop) | Maven `org.e7vm:e7vm` (jar with JNI libraries for Linux, macOS, Windows) | Java/Kotlin: `platform/java/org/e7vm` |
| iOS / macOS (Swift) | Swift Package Manager: `.package(url: "https://github.com/e7lang/e7.git", from: "0.1.0")`, or `platform/ios/build_xcframework.sh` → `build-ios/e7vm.xcframework` | Swift: `.product(name: "E7VM", package: "e7")` (`platform/ios/E7VM/E7VM.swift`) |
| Windows | `platform/windows/build_windows.ps1 [-ClangCl] [-Shared]` (VS 2022) | C/C++, C#: `platform/windows/E7VM.cs` |
| .NET (Windows / Linux / macOS) | `dotnet add package E7VM` | C#: NuGet package [`E7VM`](https://www.nuget.org/packages/E7VM) |
| WebAssembly (Node.js, browsers) | `emcmake cmake -S . -B build-wasm && cmake --build build-wasm` → `e7wasm.js` + `e7wasm.wasm` | JavaScript: npm `@e7lang/e7` ([packages/e7](packages/e7)) |

CMake options:

| Option | Effect |
|---|---|
| `E7_WITH_COMPILER` | On-device compiler (default ON) |
| `E7_BUILD_SHARED` | Build the shared library, which exports only the `e7_*` C API |
| `E7_BUILD_JNI` | Build the JNI library |
| `E7_SANITIZE` | ASan + UBSan |
| `E7_NO_COMPUTED_GOTO` | Use switch dispatch |

## Security and status

e7vm is built for running code that comes over the network. Before release it went through seven
security reviews (each by a separate AI review agent; no third-party audit yet), differential testing
against Node.js/OpenSSL, sanitizers, and fuzzing, and every finding was fixed with a regression test. The
evidence is in [docs/PRODUCTION.md](docs/PRODUCTION.md). E7 0.1.0 is the first release: thoroughly tested and
hardened, but it has not yet run in production, and releases before 1.0 may change the language, the C API or
the bytecode format (see [docs/ROADMAP.md](docs/ROADMAP.md) for the road to 1.0). Recommendations for apps:

1. **If scripts are downloaded at runtime, turn on signature checking** (`require_signature` plus the public key built into the app) and keep the private key on the build server.
2. **Set an instruction budget and a memory limit** (for example 16–64 MiB).
3. **Pass secrets as `KeyHandle`s**, never as string or byte arguments.

## Testing

```bash
python3 tests/run_tests.py     # language/library tests, byte-for-byte against Node.js + error cases
ctest --test-dir build         # all suites: C API, re-entrancy, malicious modules, fuzzers, locale
tests/tsc/check.sh             # every test and example type-checks with tsc --strict
build/e7 test examples/tests   # unit tests written with e7:test
```

CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs on every push:

- **Linux:** GCC, Clang 18, Clang 16, ASan+UBSan, switch dispatch, shared library, 32-bit.
- **macOS:** Release and ASan.
- **Mobile:** iOS, Android NDK, JNI.
- **Windows:** MSVC and clang-cl.
- **TypeScript:** `tsc` strict check.
- **Performance:** instruction ticks / peak heap / cachegrind instruction counts against `bench/baseline.json` (see [Performance](#performance)).

[`.github/workflows/maven.yml`](.github/workflows/maven.yml) builds the Maven artifacts and runs the Java
test suite on Linux, macOS, Windows and an Android emulator.
[`.github/workflows/js.yml`](.github/workflows/js.yml) builds the WebAssembly module, then tests the packed
npm tarball (`node --test` on Node 18/20/22, Linux/macOS/Windows, plus headless Chromium) and the VS Code
extension (integration tests in VS Code under xvfb), and packages the `.vsix`.

## Performance

These numbers are from x86-64, Release build, in milliseconds (scripts in `bench/`). They compare
against Node.js 22, both V8's interpreter (`--jitless`, which is pure interpretation like e7vm and the
only option on iOS) and the full JIT.

| Benchmark | e7vm | V8 --jitless | V8 JIT |
|---|---:|---:|---:|
| fib(30), recursive calls | 58 | 70 | 10 |
| 30M-iteration loop (top-level globals) | 369 | 518 | 45 |
| Sieve of Eratosthenes, 5M | 298 | 465 | 91 |
| 4M allocations + 2M method calls | 395 | 739 | 38 |
| SHA-256 in pure script (u32), 1 MiB | 168 | 1159 | 41 |
| String concat/split/Map | 384 | 293 | 243 |

In signing scripts, standard algorithms (HMAC, RSA, ECDSA, …) run natively, so a typical call takes
well under a millisecond.

`e7 --stats script.ts` prints the instruction ticks a run consumed (`e7_ticks_used()`; the same unit as
the instruction budget, and the same on every machine), the GC collections with their longest and total
pause, and the peak heap. CI guards against regressions with
[`scripts/bench.py`](scripts/bench.py): ticks and peak heap of every `bench/*.ts` must equal
[`bench/baseline.json`](bench/baseline.json), and the machine instructions executed under cachegrind may
not grow by more than 3% (see [CONTRIBUTING.md](CONTRIBUTING.md#performance-baseline)).

**GC pauses.** [`bench/gc_pause.cpp`](bench/gc_pause.cpp) builds a linked list of N nodes (a class holding a
string, an array and the next node: 3 heap objects each) through the C API and times full collections
(`e7_collect_garbage`) with all of it live and after the script dropped it. The collector is a
stop-the-world mark-and-sweep, so pauses grow linearly with the live heap; size budgets and memory limits
accordingly when scripts keep large structures alive. Apple M-series, Release, best of 4 runs:

| Nodes | Heap objects | Live heap | GC, all live | GC after drop | Automatic GCs while building (longest) |
|---:|---:|---:|---:|---:|---:|
| 10k | 30k | 1.5 MB | 0.07 ms | 0.19 ms | 1 (0.08 ms) |
| 100k | 300k | 15 MB | 1.3 ms | 3.4 ms | 5 (0.9 ms) |
| 300k | 900k | 45 MB | 7.7 ms | 11.7 ms | 7 (7.1 ms) |
| 1M | 3M | 150 MB | 29 ms | 34 ms | 9 (24 ms) |

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DE7_BUILD_BENCH=ON && cmake --build build --target e7_gc_pause
build/e7_gc_pause                 # or: build/e7_gc_pause 50000 2000000
```

## Repository layout

```
include/e7/e7.h     C API                      lib/e7.d.ts      types for IDEs / tsc
src/compiler/       parser, type checker, codegen
src/core/           bytecode format, serializer, verifier, disassembler, JS number formatting
src/vm/             interpreter, object model, GC
src/stdlib/         built-ins                  src/crypto/      e7:crypto, module signatures
src/capi/           C API                      tools/           e7, e7c command-line tools, e7wasm
platform/           Android (JNI) / Java, iOS (Swift), Windows (C#)
packages/e7/        npm package @e7lang/e7     editors/vscode/  VS Code extension
examples/           sample scripts + embedding example (examples/embed)
tests/ bench/ docs/                            third_party/     BearSSL (MIT), Monocypher (BSD-2/CC0), fdlibm (V8 port, BSD)
```

## Documentation

- [docs/SPEC.md](docs/SPEC.md): language specification (syntax, types, numeric semantics, standard library, differences from TS)
- [docs/TYPESCRIPT.md](docs/TYPESCRIPT.md): TypeScript feature coverage table (supported, partial, unsupported, and the E7 alternative)
- [docs/EMBEDDING.md](docs/EMBEDDING.md): C API, Android/iOS/Windows integration, security guidance
- [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md): delivering modules to apps: keys and rotation, signed manifests, staged rollout, kill switch, rollback, VM versions, limits, error reporting, shadow runs
- [docs/BYTECODE.md](docs/BYTECODE.md): `.e7b` format, instruction set, verification rules, signature format
- [docs/DEBUGGING.md](docs/DEBUGGING.md): `e7 debug`, debug information, the C debug API and the WebAssembly JSON protocol
- [docs/DESIGN.md](docs/DESIGN.md): design decisions
- [docs/ROADMAP.md](docs/ROADMAP.md): what 0.1.0 contains and what comes next (production pilot, 1.0)
- [docs/PRODUCTION.md](docs/PRODUCTION.md): verification record (tests, reviews and evidence)

## License

See [LICENSE](LICENSE). Third-party code: BearSSL (MIT), Monocypher (BSD-2-Clause / CC0) and V8's port of fdlibm
(Sun Microsystems fdlibm license and V8's BSD-3-Clause license; `Math` functions), in `third_party/`.
