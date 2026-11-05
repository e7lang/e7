# Changelog

All notable changes to E7 / e7vm are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

**Versioning while E7 is 0.x**: a minor release (0.1 → 0.2) may contain incompatible changes to the language,
the C API or the bytecode format; each one is listed under *Changed* or *Removed* with what to do. A patch
release (0.1.0 → 0.1.1) is compatible. The bytecode format keeps its own version number in the `.e7b` header,
and a VM rejects a module it cannot run at load time rather than running it incorrectly.

## [Unreleased]

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

[Unreleased]: https://github.com/e7lang/e7/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/e7lang/e7/releases/tag/v0.1.0
