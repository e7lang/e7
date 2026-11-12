# Changelog

All notable changes to E7 / e7vm are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

**Versioning while E7 is 0.x**: a minor release (0.1 → 0.2) may contain incompatible changes to the language,
the C API or the bytecode format; each one is listed under *Changed* or *Removed* with what to do. A patch
release (0.1.0 → 0.1.1) is compatible. A module's header names the oldest e7vm that can run it (`requires`), and a
VM rejects a module it cannot run at load time rather than running it incorrectly.

## [Unreleased]

## [0.1.6] - 2026-11-12

The slowdowns of 0.1.2 are won back, and Swift Package Manager builds get the faster interpreter loop. The
language, the C API and the bytecode format are unchanged.

### Fixed
- Two slowdowns that came with the correctness fixes of 0.1.2 and were not seen because the performance gate
  could not fail (see 0.1.5). The instruction ticks of `loops`, `objects_bench`, `sieve`, `sha256_script` and
  `strings_bench` are those of 0.1.1 again (`loops` 240 to 180 million, `sieve` 228 to 199 million):
  - Top-level code set a `let` / `const` variable's "declaration has run" flag again at every assignment to it
    (two extra instructions per assignment). Only the declaration sets it.
  - `a[i] = v`, `o.f = v` and compound assignments copied the object and the index to temporary registers before
    evaluating the right-hand side. The copy is needed only when the right-hand side assigns to a variable
    (`a[i] = i++`); otherwise the registers are used directly.
- Swift Package Manager builds used switch dispatch for the interpreter. With Xcode 17 and later they now use
  computed goto, like the CMake builds (Xcode 15 and 16 keep switch dispatch), and they get the loop fix of 0.1.5:
  a numbered assembler label before every handler's jump keeps Clang from merging the ends of the handlers, in
  any build system. On the benchmarks this is up to 1.8 times faster than switch dispatch (`sha256_script`), and
  3 times for the loop of 0.1.5.

## [0.1.5] - 2026-11-11

A faster interpreter loop in Clang builds, a comparison with QuickJS, Hermes and Lua, and two CI gates that
could not fail. The language, the C API and the bytecode format are unchanged.

### Fixed
- Clang builds: loops in a function that add to a variable and then count, such as
  `for (let i = 0; i < n; i++) s += i % 7`, ran up to 4 times slower than the same loop at the top level (measured
  with Apple clang 21 on arm64: 310 ms, now 85 ms for 30 million iterations). Clang's tail merging made the
  instruction handlers of the interpreter share one "store the result and jump to the next instruction" block;
  the CMake build now turns it off for `src/vm/interp.cpp`. The Android arm64 runtime grows by 10 KB. Builds with
  Swift Package Manager do not get the flag yet.
- CI: the performance gate (`perf` job) and the nightly differential fuzzer piped their output through `tee`, so
  the step had `tee`'s exit status and passed when the check failed. Both steps now use `set -o pipefail`.
  - `bench/baseline.json` was still the one of 2026-11-05 and the gate had failed, unseen, since before 0.1.0. It is
    now the one measured by CI for the current code. What it had hidden: since 0.1.2 `loops` takes 33% more
    instruction ticks, `objects_bench` 17%, `sieve` 15% and `sha256_script` 3% (the 0.1.2 correctness fixes), and
    against the old baseline the machine instructions grew by 21% (`loops`), 8% (`objects_bench`, `sieve`), 5% (`fib`)
    and 3% (`strings_bench`, `sha256_script`).
  - The fuzzer reported three mismatches with Node.js on 2026-11-09 and 2026-11-10 in runs marked as passed. None
    is a wrong result of e7vm: the generator (`tests/difffuzz/gen.py`) produced programs with documented differences
    (SPEC section 9). It now avoids them:
    - an indexed read or write whose index expression calls a function that shortens the array (an out-of-bounds
      read is an error in E7): the generated `numAt()`, `setNumAt()` and `charAt()` read the length after their
      arguments are evaluated;
    - `parseInt(s, 36)` of a value above 2^53, where Node.js on x64 (the CI runner) and on arm64 give different
      last digits and e7vm gives those of arm64: at most 10 digits are parsed.
  - Three generated programs were rejected by `tsc` 5.6 ("'>' expected") although they are valid JavaScript and
    run on Node.js: `a << f((x > (y)))` and `a << (new Array<number>(n))`, where `tsc` tries `<...> (` as type
    arguments. The generator writes `a << +b`.
- SPEC section 9 had two rows for `parseInt` of large values that contradicted each other; the outdated one
  ("correctly rounded") is removed.

### Added
- `docs/COMPARISON.md` and `bench/compare`: speed and size compared with QuickJS, Hermes and Lua.

## [0.1.4] - 2026-11-09

From the first runs of the new AI benchmark (`bench/ai`: 40 tasks graded by hidden tests, see its README) and the
issues found while writing its tasks.

### Fixed
- `new C(...)` in another module than `C`'s: the constructor's parameter types were looked up in the calling module
  (`unknown type 'Def'` unless the caller imported the type too, or the caller's own type of that name).

### Changed
- One compile reports all the unsupported syntax of a file: index signatures, `keyof` / `typeof` / indexed access /
  intersection / conditional types, regular expressions, `for...in`, `in`, `delete` and `var` are reported and parsed
  past, and the other errors of the file are reported too (before, the first of them stopped the compile). Errors are
  listed in source order.
- The `e7:encoding` decoders (`hexDecode`, `base64Decode`, `base64UrlDecode`, `base32Decode`, `pemDecode`) throw a
  catchable `SyntaxError` on invalid input; they ended the call before (and still do on earlier VMs). The Node.js
  shim checks the input the same way.
- TypeScript that tsc accepts and E7 rejected: `catch (e) { (e as Error).message }` (a caught value is always an
  Error in E7); a variable narrowed to a non-nullable type before a loop stays narrowed when every assignment in the
  loop stores a non-nullable value (`for (...) v = v + "!"`, `v = v.trim()`); `new Uint8Array([])` (and the other typed
  arrays); `Promise.resolve<void>(undefined)` and `later<void>(undefined)`.
- `Iterable<T>` / `ArrayLike<T>` give an error that names what to write instead (`T[]`, or `IterableIterator<T>`).
- A missing export of `e7:crypto`, `e7:encoding`, `e7:json` or `e7:test` without a close name lists the module's
  exports.

## [0.1.3] - 2026-11-08

TypeScript that tsc accepts and E7 rejected: the third batch of the second round of testing (about 120 such programs;
70 now compile and match Node.js, most of the rest are documented E7 rules), with the programs of the earlier rounds
re-run against Node.js to check that nothing changed for them.

### Fixed
- A nested function declaration read a variable of its block declared after it through a stale binding
  (`let z = 5; function q() { return z; } z = 6; q()` gave `0`; a `const` read as `null`).
- `i?.o.x` with `i` never null and `o` optional printed `undefined` (Node.js throws); it is a compile error, as in tsc.
- `parseInt` of large values in a radix that is neither 10 nor a power of two differed from Node.js in the last
  digits; typed array and `ArrayBuffer` lengths are converted as JavaScript does (`new Uint8Array(2.5)` has length 2).

- From a third round of testing (five areas, about 1,800 programs, all compared with Node.js): a `switch` clause
  reached by falling through from the one before it was narrowed by its own values only; `lbl: if (...) { break
  lbl; }` counted as an exit; assignments on the right of `??`, `??=`, `||=` and `&&=` counted as always run (all
  three let null values through as non-null); a closure reading a name its block declares later read an outer
  variable of that name (now a compile error); a nested function declaration called before a `let` / `const` it
  reads was declared read `0` / `null` (now a `ReferenceError`); `for...of` over tuples and `values()` /
  `entries()` of typed arrays read a snapshot; `e.name = s` changed what `instanceof` sees; an object literal's
  `toString` property holding a function was not used by `String()`; a nested function with default parameters
  that captures variables failed with "function signature mismatch".
- From a fourth round (about 300 more programs): the clauses of a `switch` did not share one scope (a `let` of one
  clause written in another wrote an outer variable; a captured variable of a clause failed with "invalid cell
  access"); a spread or `Object.assign` source property of type `T | undefined` holding undefined did not overwrite
  the earlier value; a static field of a nullable number type with an initializer was an internal compiler error;
  `JSON.stringify` of an error ignored an assigned `name`, `structuredClone` of a renamed error kept its name.

### Added
- Narrowing as tsc does it: a declaration's initializer, the ends of blocks and of `if` branches (nested ones too),
  `do...while` bodies, `while` conditions when the loop ends and assignments in conditions
  (`while ((x = pop()) !== undefined)`), optional chains (`if (a?.b !== undefined) a.b`), `s === "a"` and
  `case "a":` on a `string` (to the literal type), the `default` of a `switch` after `case null:`, type predicates of
  arrow functions, and `filter` / `find` with a type predicate.
- Literal types: constants where a literal type is expected by built-in methods (`m.set("n", 1)`), `"n" as Dir` and
  `s as Dir`, a type argument inferred from the expected result (`const d: Dir = id("n")`), `switch` over a boolean
  with `case true:` / `case false:` as exhaustive, `Math.max(...literals)`, string enums with the string methods and
  numeric enums as indexes, with `~` and `++`.
- Classes: an optional property of an implemented interface that the class does not declare (undefined through the
  interface; needs VM 0.1.3); a subclass declaring a base class field again; the private static members of a
  generic class used by its instances.
- Built-ins: `undefined` or a possibly undefined value for optional arguments; `push` / `unshift` with several and
  spread arguments; `includes(x, from)`; `reduce<T>(..., [])` and an annotated accumulator; `Array<T>(n)`,
  `new Array(a, b)`, `Array.of<T>()`; `new Map([["a", 1]])` without type arguments; destructuring a `Set`, `Map`,
  string or typed array; `entries()` / `keys()` / `values()` of typed arrays in `for...of`; `Promise.reject<T>`;
  `e.message = s` and `e.name = s` (VM 0.1.3); `TypeError`, `RangeError`, ... as types; `ReferenceError` and
  `EvalError`.
- Tuples whose elements have one type are iterable and have the array methods that do not change them.
- `x ??= v` has the non-null type; `x! += 1`; constant defaults (`5n`, `null`, a literal) for possibly undefined
  arguments; `[null, undefined, "a"]`; spreading a possibly undefined object; `async (): Promise<T> => p`;
  `return;` next to `return v`; stacked labels (`a: b: for`); the `const` function values of a block calling the
  ones declared after them; `x: T | undefined = d` before a required parameter.
- Clearer errors for a class method implementing an interface's function property, a class field for an optional
  interface property, and a class without an interface's optional property and without `implements`.
- Also from the third round: `x += v` and `x++` of a nullable `x` narrowed to a number; `c ? a : b` for an optional
  property; `x ?? "all"` of a literal type; a `switch` over a nullable value with `case null:` as exhaustive; `T`
  inferred from callbacks returning `Promise<T>` and from array literal arguments; `Promise.resolve<T>`;
  `.catch` handlers returning nothing; `filter((x) => x)`; `super.m()` in static methods; tuples in `Array.from`,
  `new Set` and spreads; parameters `x: T | undefined = d` are `T` in function bodies; a structuredClone that
  cannot copy throws a catchable `DataCloneError`.

### Changed
- RangeError messages are worded as in Node.js (`Invalid count value: -1`, `Invalid code point`, `Invalid string
  length`, `toString() radix argument must be between 2 and 36`).
- A module requires VM 0.1.3 only when it has a class with an absent interface property or assigns `e.message` /
  `e.name`; other modules still run on 0.1.0.

## [0.1.2] - 2026-11-08

Fixes found by a second round of testing: about 330 programs in six areas (classes and generics, numbers and
strings, collections, control flow and async, binary data and JSON, the type system) compared with Node.js.
This release fixes the programs that E7 accepted and then ran differently from Node.js; where E7 cannot behave the
same, the program is now a compile error or the difference is listed in SPEC section 9.

### Fixed
- Evaluation order: an assignment evaluated its value before the target's object and index (`a[i++] = i`,
  `obj().p = f()`, `o.x = (o = other, 1)`); `a[i++] ??= v` (also `||=`, `&&=`) evaluated the target twice, and so
  did `x = arr[i++].n++` with an accessor `n`.
- `for (let [i] = [0]; ...)` (a destructuring `let` in a `for` loop) shared one binding among the closures of all
  iterations.
- `{ ...(c ? a : b) }`, `Object.assign(t, c ? a : b)` and `const { ...rest } = c ? a : b` lost properties when a
  branch was not an object literal (0.1.1 fixed the case of two literals). Array literals of object literals of
  different shapes (`[c ? { v: 1 } : {}, { v: 2 }]`) and of object literals with another object type
  (`[p, { id: 5, meta: 6 }]`) have the type tsc gives them instead of their common properties only.
- An explicit `undefined` did not take a parameter's default when the parameter's type includes `undefined`
  (`function f(n: number | undefined = 3)`).
- A value of type `never` (the `assertNever(x)` pattern, after a number was cast to an enum it is not a member of)
  printed as `""` or `undefined`; it keeps its value.
- Object rest (`const { id, ...rest } = p`) created the optional properties that `p` does not have.
- `toString()` was chosen by the static type: through a base class, an interface, a nullable type or for an object
  literal's own `toString()`, `String(x)`, templates and `+` printed `[object Object]`. `join`, `%s` and the default
  `sort()` call it too now. `"" + x` calls a class's `valueOf()` first, as JavaScript does.
- `JSON.stringify` calls `toJSON()` / `toJSON(key)` of classes and object literals.
- Typed arrays other than `Uint8Array`: `sort(cmp)` sorted a copy instead of the array; `toSorted()` sorted the
  numbers as strings; `join` / `String(x)` printed `-0`; `for...of` read a snapshot; `JSON.stringify` of an empty
  `BigInt64Array` threw.
- Promises: `finally` on a promise of a number or integer passed on a garbage value two ticks early, and ignored a
  rejected promise returned by its callback; `then` / `catch` did not check callbacks passed by name (a function of
  another class's parameter read the wrong field; a rest parameter crashed).
- `toUpperCase` / `toLowerCase` implement full Unicode case mapping (generated from Node.js), with the Greek final
  sigma; `"Ÿ".toLowerCase()` gave `"Ź"`, and many Latin, Greek and Cyrillic letters did not convert.
- A top-level `let` / `const` read or written by a function before its declaration ran gave `0` / `null`; it throws
  a `ReferenceError` as in JavaScript.
- `declare` fields come after the initialized fields in the key order (as JavaScript adds them in the constructor).
- An internal compiler error for a value of type `null` on the left of `??` with a number or boolean on the right.

### Changed
- `map.keys()`, `values()` and `entries()` (also of `Set`) are copies of the elements, not live iterators: they are
  accepted only where JavaScript also reads them all at once (directly in `for...of`, a spread, `Array.from`,
  `new Set` / `new Map`, `console.log`). Store `[...m.keys()]` to keep them.
- Expressions of number literals for an integer type are range-checked like a single literal
  (`const x: u8 = 255 + 1` is an error instead of `0`); `1 + 2 + 3`, `~0` and `1e2` are accepted.
- A subclass that overrides only the getter (or only the setter) of an inherited accessor is an error: in
  JavaScript it would have no setter (getter). Declare both.
- The callbacks of a typed array's `forEach`, `map`, ... cannot take the array as a third parameter (they got a copy).
- `toSpliced()` needs a start index (as TypeScript declares it); a property name with an unpaired surrogate is an error.
- `Object.freeze(array)` has the type `readonly T[]`, as in TypeScript.
- A module that sorts a typed array with a comparator requires e7vm 0.1.2 (native `TA.sortCmp`); others still run
  on 0.1.0. The fixes inside the VM (`toString`, `toJSON`, `finally`, `join`) apply to modules run on 0.1.2.
- SPEC section 9 lists the differences that remain (fields read by a base constructor before the subclass's
  initializers, a never-assigned `declare` field, `Object.freeze`, `JSON.parse` key order, typed array callbacks,
  and a few others).

## [0.1.1] - 2026-11-07

Fixes found by writing six realistic scripts (pricing rules, form validation, feature flags, a BLE device protocol,
API data to view models, an order state machine with a signed log) as ordinary TypeScript and porting them.

### Fixed
- Wrong results without an error: `{ a: 1, ...(c ? { badge: "VIP" } : {}) }` dropped `badge` (a conditional of
  object literals now has the type tsc gives it, `{ badge?: string }`); a constructor parameter property whose type
  includes `undefined` (`constructor(readonly reason?: string)`) lost the value passed to it.
- Compiler crashes: `{ ...{} }` crashed `e7c`; an exhaustive `switch` whose `default` clause read the switched
  property (`default: return "unknown " + r.kind`) failed with an internal compiler error.
- Valid TypeScript that was rejected: a literal assigned to a nullable literal union
  (`let x: "a" | "b" | undefined = "a"`); `x! / 2` (the `/` was read as the start of a regular expression);
  `switch` on a nullable value (`string | undefined`, with `case undefined:`); narrowing after
  `if (!x) { x = new X(); m.set(k, x); }`; an argument that may be `undefined` for a parameter with a constant
  default; `async ({ id }) => log(id)` where a function returning `Promise<void>` is expected; a `number` converted
  to a numeric enum (`bytes[0] as MsgType`, or assigned); a computed index into a tuple whose elements have one
  type (`v[i]` with `v: [number, number, number]`).
- The `default` clause of an exhaustive `switch` reads the value as it is: a number cast to an enum that is not one
  of its members prints as that number instead of as `0` or `""`.
- `JSON.parse(text) as T` and `tryParse` check literal types and enums (`kind: "percent" | "tier"`,
  `level: Level`): a value outside them is a mismatch (`expected "percent" | "tier" at $.kind`) instead of a field
  holding a value its type does not allow. Enums can now be parsed into at all. A module that parses into such a
  type requires e7vm 0.1.1 (natives `JSON.parse2` / `JSON.tryParse2`); other modules still run on 0.1.0.

### Changed
- A number constant assigned to a numeric enum must be one of its values (`const k: Kind = 5` is an error when
  no member is 5), as in tsc.

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
  underneath, with only the 16 BearSSL source files these use vendored). No MD5, SHA-1, SHA-3, AES-CBC/CTR, RSA or
  ECDSA: a host function covers a legacy protocol.
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

[Unreleased]: https://github.com/e7lang/e7/compare/v0.1.6...HEAD
[0.1.6]: https://github.com/e7lang/e7/compare/v0.1.5...v0.1.6
[0.1.5]: https://github.com/e7lang/e7/compare/v0.1.4...v0.1.5
[0.1.4]: https://github.com/e7lang/e7/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/e7lang/e7/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/e7lang/e7/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/e7lang/e7/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/e7lang/e7/releases/tag/v0.1.0
