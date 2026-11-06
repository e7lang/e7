# Verification record (0.1)

This file records the verification items and evidence for e7vm, for review. Automated items run continuously in CI (`.github/workflows/ci.yml`).

**Status**: E7 0.x has been thoroughly tested and hardened but has **not yet run in production**. The security reviews in
section 3 were each performed by a separate AI review agent with no knowledge of the other reviews' conclusions; **no
third-party audit has been done yet**. E7 will be called production-ready only after a production pilot and an external
audit of the verifier, the loading path, the C API and the crypto wrappers.

## 1. Correctness

| Item | Method | Result |
|---|---|---|
| Language/standard library semantics | `tests/run_tests.py`: 82 cases, compared **byte-for-byte** against Node.js 22 output (differential testing) + expected output + compile-error cases | All pass |
| TS compatibility | `tests/tsc/check.sh`: tsc 5.6 `--strict` checks all cases and examples | All pass |
| Differential fuzzing (0.2.5) | `tests/difffuzz`: random well-typed programs in the E7 subset, each checked with tsc 5.6 `--strict`, run on e7 and Node.js; mismatches minimized by delta debugging. ctest `difffuzz_smoke` (40 programs) on every build, 5,000 per night (`.github/workflows/difffuzz.yml`) | About 44,000 programs locally (Node.js 24); 10 standard library bugs fixed (`tests/cases/lang/difffuzz_regressions.ts`), compiler issues reported; final 10,000 (seeds 40–44): no mismatch. `Math` results compared exactly since the fdlibm port (below; automatic for x64 builds of node): 4,500 programs on Node.js 24 arm64, the only 2 mismatches are `Math.cos(1000)`, where that Node.js build contracts fdlibm into fused multiply-adds |
| `Math` functions and `**` (0.2.5) | V8's fdlibm (`src/base/ieee754.cc`) ported to `third_party/fdlibm` instead of the C library; `tests/cases/lang/math_fdlibm.ts` (compared with Node.js) and `math_pow_fdlibm.ts` (`.out`: Node.js 22 pow). Random and special arguments (whole double range, ±0, ±Infinity, NaN, subnormals, near multiples of π/2, huge trigonometric arguments, near 1, integer and half-integer exponents, negative bases, overflow/underflow boundaries), compared bit for bit: (1) the e7 binary against the port: 2,000,000 arguments for each of the 22 fdlibm functions and `**`, and 2,000,000 for each of `sqrt fround round trunc sign clz32 abs floor ceil hypot (2 and 3 arguments) imul max min` against Node.js (72 million results, macOS arm64), plus 100,000 per function with e7 built for x86-64 (Rosetta) and with ASan+UBSan; (2) the port compiled with FMA contraction as Node.js for arm64 is, against Node.js 24 arm64 (`--no-use-std-math-pow`): 44 million results; (3) V8's source compiled for x86-64 (as Node.js x64; even with `-ffp-contract=fast` the baseline ISA has no FMA) against e7: 44 million results | No difference. Node.js itself differs: its arm64 builds give another last bit for about 0.1% of `sin`/`log`/... results (FMA contraction), and Node.js 23+ uses the C library's pow (V8 `--use-std-math-pow`; 2% of the pow arguments above differ on macOS); e7 = Node.js 22 on x64 everywhere. Before (C library): with macOS libm, e.g. 1.6% of the `sin` and 2.2% of the `pow` results of a 100,000-argument sample differed, and on arm64 builds `Math.hypot` with 3 arguments and `parseInt` beyond 2^53 in radixes that are not powers of two were contracted into fused multiply-adds (now `-ffp-contract=off` / `FP_CONTRACT OFF`; e7 on arm64 = e7 on x86-64, checked with an x86-64 build under Rosetta) |
| C API | `tests/capi_test.cpp`: argument encoding/decoding, host functions, KeyHandle, signatures, budget suspend/resume, memory/stack limits, reuse after errors | Pass |
| Locale independence | `tests/locale_test.cpp` (number parsing/formatting under de_DE) | Pass |
| Bindings | JNI (OpenJDK 21 `E7VMTest`), iOS xcframework build, Android NDK build, C# (Windows) | Pass |
| GC stress (0.2.5) | `-DE7_GC_STRESS=ON` + ASan/UBSan: the whole ctest suite with a collection at every safepoint and every allocation where one is allowed (CI job `gc-stress`), including inside callbacks from natives | Pass (found while reviewing where collections may run: the garbage of callbacks from natives, e.g. a sort comparator, was never collected before the native returned; fixed) |
| Allocation failures (0.2.5) | `tests/alloc_fail_test.cpp`: the N-th allocation fails for every N in every language/library test and example (also with every later one failing, with a budget slice of 1, under a stepping debugger); clean `E7_ERR_MEMORY`, consistent heap accounting (`VM::checkHeap`), heap back at its baseline after a failed load, VM usable afterwards | Pass (found: a module whose initializer failed stayed allocated for the life of the VM; fixed) |
| Equivalence runs (0.2.5) | `tests/equiv_test.cpp`: all test programs with a budget slice of 1 and under a single-stepping debugger inspecting every scope; switch dispatch in CI. Output identical to a normal run | Pass (found: up-front native charges failed with `E7_ERR_BUDGET` in suspend mode; fixed) |
| Soak (0.2.5) | `tests/soak_test.cpp`: one VM, 100,000 calls (1,000,000 locally) plus a failing module load every 1,000 calls; heap after GC, GC statistics and process heap flat | Pass |
| Threads (0.2.5) | `tests/threads_test.cpp` under ThreadSanitizer (CI job `tsan`): concurrent compilation, execution, slicing, cross-thread `e7_debug_pause` | Pass (found: a data race in the compiler; fixed) |
| Forward compatibility (0.2.5) | `tests/compat/forward_compat.py` (CI job `forward-compat`): modules from the current `e7c --target 0.2` on the 0.2.1 and 0.2.0 VMs; newer modules rejected with `E7_ERR_VERSION` | Pass on 0.2.1; on 0.2.0 two programs differ by the 0.2.1 fixes (Error values in `instanceof` / `String` / `JSON.stringify`, the JSON nesting-limit message) |
| Coverage (0.2.5) | `scripts/coverage.py` (CI job `coverage`, llvm-cov over the whole ctest suite; summary and artifact) | Lines 93.7%, functions 98.3% (local run; before the new tests 92.6% / 96.7%). The 80 natives no test called (Math functions, integer-keyed Map / Set / arrays, the `get()!` / `get() ??` / `pop()!` fast paths, ...) and the uncovered C API, stack-trace and disassembler paths now have tests |

## 2. Platform and compiler matrix (CI)

Linux GCC 13 / Clang 18 / Clang 16, 32-bit i386, switch dispatch, shared library, ASan+UBSan; macOS arm64 Release and ASan;
Windows MSVC and clang-cl; iOS; Android NDK; JNI. Also verified locally: Clang 15/17, libc++, aarch64 (qemu), MinGW (Wine),
GCC UBSan (including `float-cast-overflow`).

**Known compiler bug and workaround**: in LLVM 15–17, CodeGenPrepare's address-mode sinking miscompiles the computed goto interpreter loop
(located with `-mllvm -opt-bisect-limit` to `CodeGen Prepare on e7::VM::execute`, then confirmed as that transform with `-addr-sink-using-gep=false`).
CMake disables the transform for `interp.cpp` on these compilers; other build methods automatically fall back to switch dispatch. See README.

## 3. Security (untrusted input)

| Threat | Mitigation | Verification |
|---|---|---|
| Tampered/forged bytecode | Ed25519 signatures (`require_signature` + `trusted_keys`); signature is checked before parsing | Wrong public key, unsigned, and single-byte tampering are all rejected (`e7 --trust`, capi_test) |
| Malicious bytecode corrupting memory | Load-time bytecode verifier (register types/ranges, jump targets, call metadata, constant indices) + runtime object shape checks | `fuzz_load`: thousands of mutated modules per CI run, zero crashes under ASan/UBSan |
| Malicious source (on-device compilation) | Parse nesting limit 160, AST depth limit 400, JSON nesting limit 256 | `fuzz_compile`: about 60,000 cumulative mutations this round, zero internal errors, zero crashes |
| Hostile arguments to `e7:crypto` / `e7:encoding` natives | Argument validation in every native (key / nonce / length checks, DER and PEM parsers), work charged to the budget | `fuzz_natives`: every native called with generated arguments, plus round-trip / tamper-detection properties (AEAD, CBC, CTR, Ed25519, ECDSA, RSA, X25519, encodings, HKDF / PBKDF2); 90,000 runs under ASan/UBSan before 0.2.0; the only report was clang's `-fsanitize=function` on BearSSL's vtable calls (C function pointers stored with generic types), now not instrumented in `e7_bearssl` |
| Infinite loops / resource exhaustion | Instruction budget (`e7_set_budget`), `memory_limit`, `stack_slots`, `max_frames`, native re-entry depth 64 | capi_test covers each one |
| Key leakage | `KeyHandle`: keys appear only in host callbacks; scripts cannot read/print them | capi_test, examples |
| Unauthorized calls | Scripts can call only explicitly registered host functions; signatures are checked at load time | capi_test |

### 3.1 First security review (bytecode loading path)

A separate AI review agent (given only the source code and the goal "no byte sequence may corrupt memory", without knowledge of our conclusions) performed a targeted review of deserialization, the verifier,
loading/linking, the interpreter, and the standard library native functions, and reproduced issues under ASan with hand-crafted malicious `.e7b` files. It found 4 issues, all fixed:

| # | Issue | Fix |
|---|---|---|
| 1 | The host signature of exported functions (`hostSig`) was not validated; the C API would convert arguments/return values using forged types (type confusion, UB on empty signature) | The verifier requires hostSig to be well-formed and to match the function's register types one by one |
| 2 | When an inline cache slot was shared by instructions of different kinds (GETN_I/F/R, SETN_*, CALLI), the hit path could read fields as the wrong type or read the vtable out of bounds | The verifier requires each IC slot to serve a single purpose; CALLI also bounds-checks the vtable on a hit |
| 3 | `concatBytes`, `Map.fromEntries`, `console.log` assumed array elements were references | Check array element types / format by the actual type |
| 4 | The key mode of `Map.new/Set.new` could disagree with the key type (hashing a double as a pointer) | Require the key mode and the key type to have the same register kind |

Also fixed along the way: several native functions converted bytecode-supplied doubles to integers before the range check (UB); all now check first (`immIndex`).
6 malicious samples are kept in `tests/malicious/` and regression-tested by `malicious_test` (C API, including ASan in CI). Areas the review confirmed correct:
deserialization bounds and integer overflow, register/constant/index ranges, jump targets (cannot jump into operand words), call arguments and metadata, closure captures,
class inheritance layout, runtime type checks, stack/frame limits, GC roots.

### 3.2 Second security review (standard library/crypto native functions, GC, C API)

| # | Issue | Fix |
|---|---|---|
| 1 | `rsaSign`: when n in the private key does not match p·q, BearSSL performs a stack buffer overflow **write** (triggerable by plain TS + a malicious key) | Validate n == p·q and 512 ≤ bits ≤ 4096 at parse time; limit PKCS#8 nesting depth |
| 2 | Converting host arguments in `e7_call` could trigger GC and collect already-converted arguments (UAF) | Pause GC during argument conversion (`VM::GCPause`) |
| 3/4 | Re-entering `e7_call/e7_resume/e7_load` from a host function, or calling `e7_collect_garbage`/`e7_abort_suspended`, corrupted execution state | The C API rejects re-entry while running (`E7_ERR_STATE`); collect/abort calls are ignored while running |
| 5 | Unbounded recursion in `String(x)`/`join` on cyclic/very deep arrays | Cyclic references print an empty string as in JS, nesting depth limit 256, output size bounded by the memory limit |
| 6/7 | `JSON.parse` type descriptors (from bytecode): unbounded recursion, out-of-bounds read, `?~` combination leaking pointers | Descriptors first undergo strict syntax validation (bounds, depth, wrapper-layer rules) |
| 8 | BigInt size was not counted toward the memory limit; quadratic algorithms on huge BigInts ignored the instruction budget | Limit of 262144 bits, limbs counted on the heap; parsing in chunks; `**` computes the exact bit count |
| 9 | Quadratic blow-up from `$\``/`$'` in `replace`; O(n·m) substring search; PBKDF2 iterations not charged to the budget | Temporary buffers bounded by the memory limit (`checkTemp`); KMP search O(n+m); PBKDF2 pre-charges the budget per HMAC count |
| Other | double→int UB in `lastIndexOf`/`toExponential`; numeric array native functions did not check element types (information leak) | Check range first; check `vt` |

Regression tests: `tests/capi_reentry_test.cpp`, `tests/malicious/` (new JSON descriptor and cyclic join samples),
`tests/cases/errors/rt_rsa_bad_key.ts`, `rt_bigint_size.ts`, `rt_replace_oom.ts`, `tests/cases/lib/string_search.ts`
(randomized differential testing against Node). In addition, 660 "edge-case number × built-in function" scripts (±Infinity/NaN/±1e300/-0/±2^53...) were run under
float-cast-overflow UBSan with zero reports.

### 3.3 Third security review (crypto correctness and encoding, including differential comparison with Node)

| # | Issue | Fix |
|---|---|---|
| 1 | `rsaVerify` public key parsing recursed without bound on nested SPKI (stack overflow) | Allow only one SPKI layer, check the rsaEncryption OID, reject trailing data |
| 2 | **Valid** DER ECDSA signatures were rejected when r/s were both shorter than the curve length (about 26% for P-521) | Custom strict DER decoder with left padding; inputs of 2× length are tried as raw first, then as DER |
| 3 | BearSSL's lenient ASN.1 let multiple non-DER encodings of the same (r,s) verify (malleability) | Strict DER: minimal length, non-negative and minimal integer encoding, no trailing data |
| 4 | An overlong RSA public exponent e / overlong dp, dq let modpow bypass the instruction budget (up to hours) | e ≤ 64 bits (same as OpenSSL); dp/dq/iq no longer than p/q |
| 5 | `ecdsaPublicKey` accepted private scalars of 0 or ≥ n | Uniformly check 1 ≤ x < n |
| 6 | Ed25519 signature verification (cofactored equation) accepted an all-zero signature on any message for an attacker-chosen small-order public key | Reject non-canonical encodings and points in the order-8 subgroup (encodings computed independently, consistent with libsodium) |
| 7 | X25519 did not reject low-order public keys (all-zero shared secret) | Constant-time check for all-zero output and report an error (RFC 7748 §6.1) |

Confirmed correct: full DigestInfo comparison for RSA PKCS#1 v1.5, PSS edge cases, rejection of ECDSA r/s equal to 0 or ≥ n, constant-time GCM/ChaCha
authentication, CBC padding, CTR counter wraparound, HKDF/PBKDF2/HMAC/all hashes, all encodings consistent with Node, no KeyHandle leakage, random number source,
module signature coverage. Regression tests: `crypto_ecdsa_der.ts` (72 DER/raw signature sets generated by OpenSSL), `crypto_ecdsa_strict_der.ts`,
`crypto_ed25519_strict.ts`, `rt_x25519_low_order.ts`, `rt_ec_scalar_zero.ts`, `rt_rsa_huge_e.ts`.

### 3.4 Release-readiness sign-off review (fresh build, re-verifying all conclusions)

The sign-off agent did a fresh Release and ASan+UBSan build from the current commit and re-ran all tests (ctest 7/7, language tests 82/82 matching Node), all attack samples
from the three reviews (all cleanly rejected), fuzzing (`fuzz_load` 28,000 runs, `fuzz_compile` 36,000 runs, 2315 structured .e7b boundary mutations),
module signature attacks (stripping/transplanting/tampering with the trailer, wrong public key, etc., all rejected), and 460 crypto differential checks (including rejection of the RSA e=3 Bleichenbacher'06 forgery,
with no case of "e7 accepts while Node rejects"). Conclusion: **READY WITH CAVEATS**; both caveats have been fixed:

| Issue | Fix |
|---|---|
| The instruction budget did not count native function work: calling heavy native functions in a loop could bypass the budget (e.g. a loop of `toString` on a 260,000-bit BigInt took about 47 hours) | Native functions charge the budget by input/output size according to a cost model (scan/output/hash/sort/BigInt quadratic); charges are settled after the call, and execution suspends or terminates at the next check. The four DoS cases terminate within 0.1–0.5 seconds under a 10M budget; example scripts are unaffected |
| With `max_frames = 0xFFFFFFFF`, `reserve(max+1)` overflowed to 0, and frame stack reallocation inside a callback caused a UAF | Compute in 64 bits and limit `max_frames ≤ 200000`, `stack_slots ≤ 4M` |
| Low severity: VM leaked on construction failure, key provider returning NULL as an empty key, host passing a NULL pointer + non-zero length, invalid `e7_type` enum values, CLI early exit without freeing | All now handled cleanly |

Regression tests: `capi_reentry_test` adds native-loop budget and `max_frames` cases.

### 3.5 Fifth security review: try/catch (0.2)

A separate AI review agent (given the source, the threat model and SPEC 3.1, without our tests' conclusions) reviewed the
try/catch added in 0.2 end to end: handler tables, THROW, the catch path in the VM, Error objects, natives and callbacks,
suspend/resume, the debugger and the compiler. It built with ASan+UBSan, fuzzed 964 random verifier-accepted handler
tables (no sanitizer reports) and reproduced each finding. **No memory-safety issue was found.** All eight findings are
fixed in 0.2.1 with regression tests:

| # | Severity | Issue | Fix |
|---|---|---|---|
| 1 | High | A crafted handler table whose target lies at or before its range made a catch loop forever without spending budget | The verifier requires every handler to come after its range, so looping needs a charged back edge |
| 2 | High | A native raising a catchable error was never charged for its work: a loop of failing `JSON.parse` calls on 4 MB ran ~12 h on a 10M budget | Natives with a cost model are charged also when they fail |
| 3 | High | Every throw built the full stack trace (O(frames), uncharged) before looking for a handler: 2,000 throws 19,000 frames deep took 2.3 s on ~14k ticks | The trace is built only for errors no handler takes; a catch charges one tick per frame it drops |
| 4 | Medium | e7's own limits (JSON / join nesting 256, BigInt 262144 bits, array and typed-array length caps) carried JavaScript error names, so `catch` saw errors Node.js would not raise | They are raised without a JavaScript name ("an e7 limit") and end the call |
| 5 | Medium | `x instanceof Error` was folded to `true` for any `Error`-typed value, including holes of `new Array<Error>(n)` | Folded only for caught values; checked at run time otherwise |
| 6 | Medium | The internal representation of Errors leaked through `JSON.stringify`, `String([e])`, concatenation of `Error \| null` and `console.log` of containers | Error objects carry a flag; conversions match Node.js (`{}`, `"Name: message"`); printing a value containing an Error is a compile error |
| 7 | Low | More than 65,535 `try` statements in one function gave an internal compiler error | A normal compile error |
| 8 | High (not try-specific) | An out-of-memory error inside `Map.set` left the map's grown storage unaccounted; clearing the map wrapped the heap counter below zero, after which small allocations failed and large ones bypassed the limit | The grown storage is accounted even when the limit is exceeded; `arrayReserve` undoes its accounting if `realloc` fails |

Areas the review confirmed correct: register kinds after a catch, frame and handler lookup (`ip - 1` always inside the
raising instruction), restoring the native-call bookkeeping (`nativeDepth`, current native, callback list, temporary
roots, GC inhibition) for nested callbacks, GC roots of the thrown object, natives interrupted by a catch (sort writes back
only on success, RAII state), inline caches, limits never being catchable, the debugger's caught / uncaught decision and
pause-to-host, Error forging (only data, never a safety issue), serialization and the compiler's handler relocation.
Regression tests: `verify_handlers_test` (handlers before or inside their range), `capi_test` (budget for failing
natives and unwinding, `Map` accounting after repeated out-of-memory), `tests/cases/lang/error_values.ts` (Node parity),
`tests/cases/errors/rt_limit_not_catchable.ts`, `catch_console_log_nested.ts`.

### 3.6 Sixth security review: the 0.2.5 VM changes (0.2.6)

A separate AI review agent reviewed everything the VM gained between 0.2.1 and 0.2.5: the garbage collector running
during native callbacks (`Array.sort` comparators, `assertThrows`), the Node.js-compatible `console.log` formatter, `Map`
iteration cursors, fdlibm, module unloading after a failed load, the test hooks, and the handler tables for `finally`.
It built Release, ASan+UBSan and ASan+UBSan with `E7_GC_STRESS`, ran hostile comparators and every exit path of
try/catch/finally under GC stress, fuzzed the 21 fdlibm functions (364k inputs) and the number formatting and parsing
(300k inputs) under the sanitizers, and reproduced each finding. **No memory-safety issue was found.** All six findings
are fixed in 0.2.6:

| # | Severity | Issue | Fix |
|---|---|---|---|
| 1 | High (0.2.5 regression) | With `suspend_on_budget`, a native that charges its work up front or as it goes (`pbkdf2`, `assertEqual` on an object graph with 2^60 paths) ran to completion whatever the slice: one call could hold the host thread for days | A native may run past the end of the slice by a bounded amount (2^22 ticks, or the slice when that is larger); past it the call fails with `E7_ERR_BUDGET`, as without suspension |
| 2 | High | The output of `JSON.stringify` (and of console.log's `%j`, which follows shared objects path by path) was not checked against the memory limit: an object graph of 2^23 shared nodes reached 1.5 GB of host memory under an 8 MB limit | The output buffer is checked against the memory limit as it grows |
| 3 | Medium | console.log's formatting was charged by its arguments only: printing one large BigInt many times took 122 s on 801 ticks | Formatting charges the text it produces, `%j` its JSON, and a BigInt its quadratic decimal conversion, as `BigInt.toString` does |
| 4 | Medium (older) | The string keys of a default `Array.sort()` were not checked against the memory limit (2.5 GB under an 8 MB limit) | The keys are checked as they are built and their comparison is charged; the buffers of `sort(cmp)` are checked too |
| 5 | Low | `parseInt` with a power-of-two radix overflowed a signed counter (undefined behaviour) for about 430 million digits; Release printed 0 instead of Infinity | The exponent saturates (the result is Infinity, as in JavaScript) |
| 6 | Low | A `Map` iteration cursor from crafted bytecode overflowed a signed subtraction (undefined behaviour; the result was still bounds-checked) | Cursors before the first entry are handled before the subtraction |

Areas the review confirmed correct: GC roots during callbacks (everything a native holds across a callback is rooted,
`gcBlockers_` is restored on every path), the inspector's bounds and memory accounting, `Map` cursors across compaction and
`clear`, `-0` keys and `Date` keys, fdlibm's argument reduction, number formatting, unloading a module after a failed
load (also with a debugger attached), the test hooks (not reachable from scripts or bytecode), the tick counter in
suspend mode, and the verifier and code generation for `finally`, generics, getters and spread. Regression tests:
`capi_test` (bounded overrun with suspension; console.log, `%j`, `JSON.stringify` and default sort against the budget
and memory limit).

### 3.7 Seventh security review: the 0.3 VM changes (coroutines, promises, presence masks, typed arrays)

A separate AI review agent (given the source, the threat model and the 0.3 design, without our tests' conclusions)
reviewed the VM side of 0.3 except the regular expression engine (reviewed separately): coroutines (`GEN_START`,
`YIELD`, `RESUME_*`, `ASYNC_START`, `AWAIT` and the async return and error boundary), the verifier rules and format-3
reading, the promise job queue, host tickets and the async C API, presence masks (`FIELD_ABSENT`), typed arrays,
`structuredClone` and the recursive JSON descriptors. It built ASan+UBSan and confirmed findings 1-3 with
proofs of concept (crafted modules and one plain script). All four are fixed:

| # | Severity | Issue | Fix |
|---|---|---|---|
| 1 | Critical | The 64-bit typed-array natives (`TA.getBig` / `setBig` / `fillBig`) did not check the array's element kind: a crafted module applying them to a byte array accessed 8-byte elements of a 1-byte array (out-of-bounds read and write) | They check that the array is a `BigInt64Array` / `BigUint64Array` (and the number natives that it is not) |
| 2 | High | `then` without an `onFulfilled` handler passed the value to the derived promise without comparing value types: crafted bytecode could make a number read as a reference | `Promise.then` requires one representation for a pass-through, and every pass-through checks it again when the job runs |
| 3 | High | `structuredClone` recursed once per nesting level: a plain script with a 400,000-node linked list overflowed the C++ stack | At most 256 levels (as `JSON.stringify`); deeper values fail with a catchable `DataCloneError` |
| 4 | Low | `unloadModule` (not reachable through the C API) could free a module while promises, jobs or coroutines of a pending call referred to it | Not while a call waits for tickets or has queued jobs |

Areas the review confirmed correct: the verifier's coroutine rules and format-3 bounds; RESUME's stack and frame
checks, `CO_RUNNING` against re-entry, coroutines finished when frames are unwound; GC rooting of promises, reactions,
queued jobs, tickets and saved coroutine registers (traced only while suspended); presence masks; the `@n` JSON
back-references (bounded recursion, checked indices); the `allSettled` record class checks; the typed-array number
paths, offsets and alignment; the async native thunk and `e7_settle`. Regression tests: `verify_coroutines_test`
(malformed coroutine bytecode, a BigInt native on a byte array), `structured_clone_deep.ts`.

### 3.8 Eighth security review: regular expressions (0.3)

A separate review agent reviewed the regular expression engine (`src/regexp`) and its integration (literals, the
module table, `RegExp` objects, match results, the String methods and the native `matchAll` iterator). Fuzzing about
2.9 million generated patterns, inputs and crafted modules under ASan+UBSan found no memory-safety issue; the
findings are budget and memory-limit bypasses, all confirmed with proofs of concept and fixed:

| # | Severity | Issue | Fix |
|---|---|---|---|
| 1 | High | GetSubstitution searched for the `>` of `$<` to the end of the replacement for every `$<` (quadratic, uncharged: 4 s for 32K ticks) and looked each `$<name>` up through all group names | The replacement is parsed once per call into pieces (the next `>` is remembered, names are looked up in a hash map) and applied per match, charged by pieces and output |
| 2 | High | `replace` built its result and the captures of all matches in C++ buffers that were not counted (1 GB at a 32 MB limit) | The result counts against the memory limit (and the string length limit) as it grows; a replacement string is applied to each match as it is found; the matches kept for a replacement function count against the limit |
| 3 | High | Pattern compilation was not charged or counted: `new RegExp` of a large pattern (33 s, 1.9 GB at a 256 MB limit), module literals at load (488 MB, 0 ticks); per-occurrence class copies, case closure over large sets, quadratic group-name checks | `re::compile` takes the Limits: about one step per character, node, instruction, class range and case-folding lookup, and the memory it builds up is reserved; identical classes and property escapes are built once per pattern; names use a map. `new RegExp` charges the budget and the memory limit; module literals are bounded at load (`kLoadRegExpWork` steps, the memory limit) and their programs are counted in the heap while the module is loaded |
| 4 | Medium | Copies sharing a run-time program (`new RegExp(re)`, the `matchAll` copy, `structuredClone`) did not count it: when the original was freed, the program stayed with the copies uncounted (680 MB at a 32 MB limit); the size was truncated to 32 bits | A run-time program is counted once while any object holds it (the last holder releases it); 64-bit sizes |
| 5 | Medium | The VM's record classes of group-name sets grew without bound and were not counted (330 MB at 32 MB) | They are bounded: at most 16384 sets and a quarter of the memory limit in size, then `E7_ERR_MEMORY` (they are kept for the VM's lifetime, since inline caches compare class pointers) |
| 6 | Low | The setup of each match (registers and captures, proportional to the number of groups) was not charged (100 times the normal cost per tick with 32,000 groups) | Each match charges registers / 8 steps |
| 7 | Info | Error messages formatted into a 512-byte buffer could be cut inside a UTF-8 sequence (a long SyntaxError message quotes the pattern) | Cut at a character boundary |

Areas the review confirmed correct: the parser's and matcher's bounds (nesting limit, explicit backtracking stacks
charged and counted, no recursion on the input); the natives against crafted bytecode (argument types checked, literal
indices checked against the module's table); match arrays and their extra block (flags copied only with the block);
the native `matchAll` iterator under `RESUME` (kinds, finished state); the lifetime of a module's literal programs
(shared with the objects made from them). Regression tests: `capi_test` (each finding ends with `E7_ERR_BUDGET`,
`E7_ERR_MEMORY` or `E7_ERR_LOAD` within the limits and in bounded time, or completes linearly), `regexp_test`
(compilation under throwing limits, the per-match charge).

**Configuration the host must apply** (see EMBEDDING.md §6): enable signature checking, set an instruction budget and a memory limit. The command-line tool `e7` is for development only
and sets no budget by default.

## 4. Performance (x86-64, Release)

See the README benchmark table; comparable to or faster than V8 `--jitless`. Clang 15–17 builds keep computed goto under the workaround (Sieve of Eratosthenes ~330 ms,
GCC 290 ms, switch fallback ~380 ms).

## 5. Known limitations (by design, not defects)

See the "Not supported" list in SPEC.md: `try`/`catch` catches only errors that Node.js also throws (E7-specific checks such as out-of-bounds reads end the call), no `finally`, no user generics, no regular expressions, no `any`; out-of-bounds array reads are runtime
errors; Date has UTC semantics; an object property set to `undefined` counts as a missing optional property (`Object.keys`,
spread, `console.log`; SPEC section 9).
