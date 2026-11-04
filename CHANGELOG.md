# Changelog

All notable changes to E7 / e7vm are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.0.1] - 2026-11-04

### Added
- **Packages for every platform**
  - Swift Package Manager: `.package(url: "https://github.com/e7lang/e7.git", from: "1.0.1")`, product `E7VM`
    (built from source; XCTest suite on macOS and the iOS simulator).
  - Android AAR `org.e7vm:e7vm-android` and desktop JVM jar `org.e7vm:e7vm` (JNI libraries for Linux x64/arm64,
    macOS, Windows bundled and loaded automatically); tested on desktop JVMs and an Android emulator.
  - NuGet package `E7VM` with native libraries for win-x64, linux-x64, linux-arm64, osx-arm64 and osx-x64.
  - npm package `@e7lang/e7`: the compiler and VM as WebAssembly for Node.js 18+ and browsers
    (`compile`, `check`, `run`, `disassemble`), plus the `e7-js` command line tool.
  - Homebrew formula and Scoop manifest for the `e7` / `e7c` command line tools.
- **VS Code extension "E7 Language"**: diagnostics from the E7 compiler as you type, *Run File*,
  *Compile to Bytecode* and *Show Bytecode* commands; works on `*.e7` files and `.ts` files that import `e7:` modules.
- **Test runner** `e7 test` and the `e7:test` module (`assert`, `assertEqual`, `assertNotEqual`, `assertClose`,
  `assertThrows`, `fail`): every test runs in a fresh VM with its own budget and memory limit; `--filter`, `--json`.
- **Better diagnostics**: "did you mean ...?" suggestions for unknown names, properties, methods, enum members,
  imports and modules; clearer messages naming the alternative for unsupported TypeScript features (`any`,
  generics, `try`/`catch`, `async`, `for...in`, regular expressions, accessors); `e7` / `e7c` show the source line
  with a caret.
- **Better stack traces**: correct file names for frames in imported modules, native frames
  (`at JSON.parse (native)`), and long recursion collapsed into one line.
- Swift wrapper: `E7VM.sign`, `E7VM.publicKey`, `E7VM.version`, `hasFunction`, `collectGarbage`.
- Fuzzing: libFuzzer targets for the loader and the compiler (`fuzz/`), ClusterFuzzLite with ASan and UBSan on pull
  requests and nightly, and a replay of the fuzz entry points over the test corpora in every CI job.
- Release automation: optional macOS signing + notarization and Windows Authenticode signing (enabled by secrets);
  packages published from the release workflow; post-release install checks with Homebrew and Scoop.
- Project docs: `CHANGELOG.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, issue forms, PR template;
  CI checks that version numbers and the SwiftPM/CMake source lists agree.

### Fixed
- Swift wrapper: the key provider handed the VM a pointer into a temporary Swift array; keys are now copied into a
  buffer owned by the VM wrapper and wiped before reuse.
- `-DE7_FUZZ=ON` referenced a missing `fuzz/` directory.

### Changed
- The C# binding's managed assembly is named `E7.dll` (namespace `E7`) so it cannot collide with the native
  `e7vm.dll` on case-insensitive file systems; the NuGet package id is `E7VM`.
- Imports from built-in modules are checked where the import is written.

## [1.0.0] - 2026-11-04

First public release.

- **Language**: E7, a strict subset of TypeScript (every E7 program passes `tsc --strict` and prints the
  same output on Node.js): classes, interfaces, enums, tuples, closures, destructuring, spread, `?.`, `??`,
  template strings, multi-file modules, `Map`/`Set`, `Uint8Array`, `bigint`, `Date`, type-checked
  `JSON.parse(text) as T`, and fixed-width integers `i8`…`u64`.
- **e7vm**: register-based bytecode VM in C++17 with a bytecode verifier, precise GC, inline caches,
  instruction budget (including native calls), memory and call-depth limits, suspend/resume.
- **Crypto and encodings** (`e7:crypto`, `e7:encoding`): SHA-1/2/3, MD5, HMAC, HKDF, PBKDF2, AES-GCM/CBC/CTR,
  ChaCha20-Poly1305, Ed25519, X25519, ECDSA P-256/384/521, RSA PKCS#1 v1.5 and PSS; hex, base64(url),
  base32, UTF-8, PEM. `KeyHandle` keeps keys in the host.
- **Optional Ed25519 module signatures** (`e7c --sign`, `trusted_keys` / `require_signature`).
- **Embedding**: C API, Kotlin/Java (JNI), Swift and C# bindings; Android, iOS, Windows, macOS and Linux builds.
- **Tools**: `e7` (run), `e7c` (compile, sign, disassemble), prebuilt binaries for Linux x64/arm64,
  macOS universal and Windows x64.
- **Website** with docs and an in-browser playground (WebAssembly): https://e7lang.github.io

[Unreleased]: https://github.com/e7lang/e7/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/e7lang/e7/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/e7lang/e7/releases/tag/v1.0.0
