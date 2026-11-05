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

## Performance baseline

The `perf` CI job runs [`scripts/bench.py`](scripts/bench.py) on every `bench/*.ts` and compares with
[`bench/baseline.json`](bench/baseline.json):

- **Instruction ticks and peak heap** (from `e7 --stats`) must match **exactly**. They are deterministic and
  machine-independent, and change only when the compiled bytecode, the standard library or the GC changes.
- **Machine instructions** (`Ir` under `valgrind --tool=cachegrind`, Linux x86-64, gcc Release, the CI
  runner `ubuntu-24.04`) may not grow by more than **3%** per benchmark.

```bash
python3 scripts/bench.py --check                 # ticks and heap (uses build/e7 and build/e7c)
python3 scripts/bench.py --check --cachegrind    # plus Ir, on Linux with valgrind
```

When a change is *meant* to change these numbers (a compiler optimization, a new GC policy, a new
benchmark), update the baseline in the same pull request and say why in the description:

1. Ticks and heap: `python3 scripts/bench.py --update` (any platform) and commit `bench/baseline.json`.
2. Instruction counts: download the `bench-baseline` artifact of the PR's `perf` job (a complete
   `baseline.json` measured on the CI runner) and commit it as `bench/baseline.json`, or run
   `python3 scripts/bench.py --update --cachegrind` on Linux x86-64 with the same configuration
   (`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`, the distribution gcc).

An Ir number more than 3% *below* the baseline passes with a note; update the baseline to lock in the
gain. Wall-clock timings (`e7 --time`, `bench/gc_pause.cpp`) are reported but never gated.

## Adding a language test

Put a program in `tests/cases/lang/` (or `lib/`, `modules/`). The harness runs it with e7 and with Node.js
and requires identical output. Programs that must fail go in `tests/cases/errors/` with the expected
message in a leading comment: `// @error: <text>` for compile errors, `// @runtime-error: <text>` for
runtime errors (the text must appear in the message).

## Differential fuzzing

[`tests/difffuzz`](tests/difffuzz) generates random, deterministic, well-typed programs inside the E7
subset (`gen.py`: numbers and their formatting, strings, template literals, arrays, tuples, `Map`/`Set`,
interfaces, classes with inheritance, closures, loops/`switch`/labels, `?.`/`??`, `try`/`catch`,
`JSON.stringify`, BigInt, fixed-width integers, `Math`, UTC dates), runs each with e7 (instruction budget
and memory limit) and with Node.js (the `tests/node_shim`, `TZ=UTC`), and compares stdout and the exit
behaviour. Each mismatch is saved under `--out` (default `build/difffuzz/<name>/`: `prog.ts`, both outputs,
`info.json`) and minimized by delta debugging to `min.ts`, which keeps the same kind of mismatch (and, with
`--tsc`, stays valid strict TypeScript).

```bash
python3 tests/difffuzz/run.py --seed 7 --count 2000                # programs 0..1999 of seed 7
python3 tests/difffuzz/run.py --seed 7 --count 2000 --tsc tsc      # also tsc --strict every program
python3 tests/difffuzz/run.py --seed 7 --start 123 --count 1 --keep   # one program, kept in build/difffuzz/programs
python3 tests/difffuzz/run.py --minimize prog.ts --tsc tsc         # minimize a mismatch in any file
python3 tests/difffuzz/gen.py --seed 7 --index 123                 # print one program
```

Programs depend only on the seed and the index. ctest runs 40 programs (`difffuzz_smoke`), the nightly
workflow [`difffuzz.yml`](.github/workflows/difffuzz.yml) runs 5,000 with a new seed each night and
uploads the mismatches as an artifact. When it finds one, decide which it is: an e7 bug (fix it and add
the reproducer to `tests/cases/lang/`), a documented difference (SPEC section 9; the generator must not
produce it), or a generator bug (an invalid or nondeterministic program; fix `gen.py`). The generator
already stays away from the documented differences: indexes go through a bounds helper, fixed-width
integer operations are wrapped in their conversion (`u32(a + b)`), structures are printed with
`JSON.stringify`. Results of the fdlibm `Math` functions (`sin`, `exp`, `log`, `pow`, ...) are compared
exactly when node is an x64 build and rounded to 12 digits otherwise (Node.js for arm64 contracts fdlibm
into fused multiply-adds; SPEC section 4.1); `--exact-math` / `--approx-math` override that. `run.py`
passes `--no-use-std-math-pow` to Node.js 23+, whose `**` otherwise uses the C library's pow.

## Commit style

Short imperative subject line (`vm: reject negative jump targets`), a body explaining *why* when it isn't
obvious, and a `CHANGELOG.md` entry under "Unreleased" for user-visible changes.

## License

By contributing you agree that your contributions are licensed under the [MIT License](LICENSE).
