# Comparison with QuickJS, Hermes and Lua

How e7vm compares in speed and size with three engines that apps commonly embed: QuickJS and Hermes
(JavaScript) and Lua. All four are interpreters without a JIT, so they are the engines that can run on iOS.
The numbers were measured on 2026-11-11 with the scripts in [`bench/compare`](../bench/compare).

In short:

- **Speed.** e7vm is faster than QuickJS on all six benchmarks (1.1 to 8 times). Against Hermes it is level on
  recursive calls, 14% slower on object allocation and 2 to 3 times faster on the rest. Lua is faster on
  recursive calls and plain loops; e7vm is faster on objects, integer arithmetic and strings.
- **Size.** Lua is about 3.5 times smaller. The e7vm runtime is about the size of QuickJS and a third of Hermes.

## Speed

Milliseconds, the fastest of 11 runs; lower is better. Apple M6 (arm64), macOS, every engine built for Release
with Apple clang 21.

| Benchmark | e7vm | QuickJS | Hermes | Lua | V8 --jitless | V8 JIT |
|---|---:|---:|---:|---:|---:|---:|
| fib(30), recursive calls | 22 | 28 | 21 | 17 | 33 | 3 |
| 30M-iteration loop (top-level variables) | 148 | 207 | 394 | 67 | 211 | 15 |
| Sieve of Eratosthenes, 5M | 87 | 238 | 268 | 80 | 166 | 28 |
| 4M allocations + 2M method calls | 152 | 416 | 131 | 344 | 245 | 12 |
| SHA-256 in pure script, 1 MiB | 47 | 372 | 127 | 159 | 329 | 12 |
| String concat/split/Map | 63 | 70 | 142 | 118 | 65 | 48 |

| Engine | Version | Build |
|---|---|---|
| e7vm | 0.1.4 | CMake `Release` |
| QuickJS | 2026-06-04 (bellard/quickjs `e585640`) | `make qjs` |
| Hermes | 1.0.0 (facebook/hermes `99e244b`) | CMake `Release`, run with `-O` |
| Lua | 5.5.1 | `onelua.c`, `-O2` |
| V8 | Node.js 24.21.0 | official binary, for reference |

How to read the table:

- **The programs.** e7vm runs `bench/*.ts`. The JavaScript engines run the same programs with the types removed
  ([`bench/compare/js`](../bench/compare/js)); the Lua versions ([`bench/compare/lua`](../bench/compare/lua)) are
  written the way a Lua programmer would write them (`local` variables, tables, integer `for` loops). Every
  engine prints the same result for every benchmark.
- **What is timed.** Each program times its own work, so startup and compilation are not included.
- **The loop benchmark.** Its variables are at the top level: globals in e7vm and JavaScript, locals in Lua.
  With the same loop inside a function e7vm takes 88 ms (QuickJS 198, V8 `--jitless` 212, Hermes 400).
- **SHA-256.** e7vm uses its `u32` type and `rotr32`, which run as machine integers. JavaScript uses `>>> 0`
  and a `rotr` function; Lua uses 64-bit integers masked to 32 bits, and a table of bytes where the others use
  a `Uint8Array`. In a real script all four would call a native hash; the benchmark measures integer
  arithmetic.
- **Six small programs** show how the interpreters differ on basic operations. They do not predict the speed
  of a particular script.

To run it again:

```bash
python3 bench/compare/run.py --e7 build/e7 --qjs path/to/qjs --hermes path/to/hermes --lua path/to/lua
```

## Size

Shared library for Android arm64-v8a, stripped. Sizes are in KB (1000 bytes); "compressed" is `gzip -9`,
close to what the library adds to an APK download.

| Library | Size | Compressed | Contents |
|---|---:|---:|---|
| Lua 5.5.1 | 260 | 115 | VM, compiler and the standard libraries |
| QuickJS 2026-06-04 | 827 | 389 | VM, compiler, regular expressions, Unicode tables; without `quickjs-libc`, Intl and Temporal |
| e7vm 0.1.4, runtime only | 933 | 385 | VM, verifier, standard library, `e7:crypto`, `e7:encoding`, libc++ linked statically; runs `.e7b` modules |
| e7vm 0.1.4, with the compiler | 2,641 | 1,050 | The above plus the on-device compiler |
| Hermes (`hermes-android` 260318099.0.7) | 2,707 | 1,142 | `libhermesvm.so` from the release AAR on Maven Central; also needs `libc++_shared.so`, `libjsi.so` and `libfbjni.so` |

How to read the table:

- **Lua, QuickJS and e7vm** were built with NDK r28 for size (`-Os`, or `MinSizeRel` for e7vm). Hermes is the
  library Meta publishes.
- **Compilers.** Lua, QuickJS and Hermes always contain their compiler, because they load source text (Hermes
  also loads bytecode). An app that receives `.e7b` modules needs only the e7vm runtime.
- **What each one includes.** e7vm includes crypto and encodings and has no regular expressions. QuickJS and
  Hermes implement all of JavaScript, including regular expressions. Lua has neither crypto nor Unicode
  tables.
- **The JNI binding** of e7vm (`libe7vm_jni.so`, the library in the AAR) adds about 27 KB to the two e7vm rows.

To measure it again:

```bash
ANDROID_NDK_HOME=/path/to/ndk bench/compare/size.sh path/to/lua path/to/quickjs hermes-android-release.aar
```
