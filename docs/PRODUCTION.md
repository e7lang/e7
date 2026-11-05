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
| C API | `tests/capi_test.cpp`: argument encoding/decoding, host functions, KeyHandle, signatures, budget suspend/resume, memory/stack limits, reuse after errors | Pass |
| Locale independence | `tests/locale_test.cpp` (number parsing/formatting under de_DE) | Pass |
| Bindings | JNI (OpenJDK 21 `E7VMTest`), iOS xcframework build, Android NDK build, C# (Windows) | Pass |

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

**Configuration the host must apply** (see EMBEDDING.md §6): enable signature checking, set an instruction budget and a memory limit. The command-line tool `e7` is for development only
and sets no budget by default.

## 4. Performance (x86-64, Release)

See the README benchmark table; comparable to or faster than V8 `--jitless`. Clang 15–17 builds keep computed goto under the workaround (Sieve of Eratosthenes ~330 ms,
GCC 290 ms, switch fallback ~380 ms).

## 5. Known limitations (by design, not defects)

See the "Not supported" list in SPEC.md: `try`/`catch` catches only errors that Node.js also throws (E7-specific checks such as out-of-bounds reads end the call), no `finally`, no user generics, no regular expressions, no `any`; out-of-bounds array reads are runtime
errors; Date has UTC semantics; the key order of optional properties after object spread is fixed at compile time.
