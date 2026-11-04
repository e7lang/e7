# Contributing to E7

Thanks for helping! Bug reports, test cases, docs fixes and code are all welcome.

## Ground rules

- **E7 stays a strict subset of TypeScript.** Every program the E7 compiler accepts must also pass
  `tsc --strict` (with `lib/e7.d.ts`) and print the same output on Node.js. New language features need a
  design discussion in an issue first.
- **Security first.** e7vm runs bytecode that may be hostile. Changes to the loader, verifier, interpreter,
  GC or natives need a regression test, and should keep the sanitizer and fuzz jobs green.
  Report vulnerabilities privately (see [SECURITY.md](SECURITY.md)), not in public issues.
- Keep pull requests focused: one fix or feature per PR, with tests.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure        # language tests (needs Node.js 22), C API, fuzz smoke tests
```

Before sending a PR, please also run:

| Check | Command |
|---|---|
| Sanitizers | `cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DE7_SANITIZE=ON && cmake --build build-asan -j && ctest --test-dir build-asan` |
| TypeScript compatibility | `npm i -g typescript@5.6 && tests/tsc/check.sh` |
| Switch dispatch | configure with `-DE7_NO_COMPUTED_GOTO=ON` |

CI runs all of these plus macOS, Windows (MSVC and clang-cl), 32-bit, Android, iOS, WebAssembly and
the language bindings.

## Adding a language test

Put a program in `tests/cases/lang/` (or `lib/`, `modules/`). The harness runs it with e7 and with Node.js
and requires identical output. Programs that must fail go in `tests/cases/errors/` with the expected
message in a leading comment: `// @error: <text>` for compile errors, `// @runtime-error: <text>` for
runtime errors (the text must appear in the message).

## Commit style

Short imperative subject line (`vm: reject negative jump targets`), a body explaining *why* when it isn't
obvious, and a `CHANGELOG.md` entry under "Unreleased" for user-visible changes.

## License

By contributing you agree that your contributions are licensed under the [MIT License](LICENSE).
