# Changelog

All notable changes to E7 / e7vm are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Contributor docs: `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, issue forms and a PR template.
- libFuzzer targets for the bytecode loader and the compiler (`fuzz/`), continuous fuzzing with
  ClusterFuzzLite (ASan + UBSan) on pull requests and nightly, and a replay driver that runs the fuzz
  entry points over the test corpora in every CI job.
- Homebrew formula and Scoop manifest generated for every release, with install checks in CI.
- Optional code signing in the release workflow: macOS Developer ID signing + notarization and Windows
  Authenticode signing, enabled by repository secrets.

### Fixed
- `-DE7_FUZZ=ON` referenced a missing `fuzz/` directory.

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

[Unreleased]: https://github.com/e7lang/e7/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/e7lang/e7/releases/tag/v1.0.0
