# E7 Language and e7vm Virtual Machine Design (0.1, implemented)

> **Implementation status (0.1)**: Everything in this document has been implemented. For implementation details, [SPEC.md](SPEC.md), [BYTECODE.md](BYTECODE.md)
> and [EMBEDDING.md](EMBEDDING.md) are authoritative. Changes from the original plan:
> - Strings are always stored as UTF-16 (no dual Latin-1 representation; concatenation in loops is kept correct by a native implementation; see the README for performance).
> - Instructions are fixed-width 64-bit (`op:16 A:16 B:16 C:16`) with up to 65535 registers, which is roomier than the originally planned 32-bit encoding.
> - The planned "runtime quickening" was replaced by emitting typed instructions directly from static types; compare-and-jump fusion, constant register hoisting and loop inversion were also added.
> - `throw new Error(msg)` is supported, with the semantics of terminating the script (there is still no `try/catch`).
> - User-defined generics, getters/setters and coroutine syntax are deferred to E8; the VM already supports suspend/resume (`e7_resume`).
> - Crypto libraries: BearSSL (RSA, ECDSA, AES, ChaCha20-Poly1305, SHA-1/2, HMAC, HKDF) + Monocypher (Ed25519/X25519);
>   SHA-3/Keccak, PBKDF2 and RSA-PSS are in-house implementations. All are cross-checked against Node.js `node:crypto`. The Chinese national (SM) algorithms SM2/3/4 are not included.

> v0.4 changes: Made it explicit that the main workload is **signing / signature verification algorithms**. Added §3.8 (design for signing): fixed-width integers and bitwise operations, byte handling, the native `e7:crypto` module, `bigint`, and KeyHandle; **proposed revising** the default type of integer literals (see §3.2); added optional bytecode hardening to the security section.
>
> v0.3 changes: Settled on the names **E7 / e7vm**; the syntax is **a strict TypeScript subset only** (the Ruby option was dropped); integer literals default to `int`; **no exceptions** (`try/catch/throw`); the workload is general math/numeric algorithms + string processing, which drives the standard library and string implementation plans.
>
> v0.2 changes: Based on feedback, switched to **strict static typing** with syntax based mainly on a **strict TypeScript subset**; made explicit that **scripts are delivered from the server**, that **an on-device compiler is optional**, and that **the design leaves room for coroutines**; added a naming survey.
> At this stage this is design only, no development.

Confirmed requirements:

| Item | Decision |
|---|---|
| Types | Strict static typing, no `any` |
| Syntax | **Strict TypeScript subset** (every E7 program is valid TS) |
| Naming | Language **E7**, engine **e7vm** |
| Numbers | Integer literals default to `int`(i64), `number` = f64 |
| Exceptions | `try/catch/throw` **not supported**; runtime errors terminate the script and return to the host |
| Workload | **Mainly signing / signature verification algorithms**, plus general math algorithms and string processing |
| Script source | **Delivered from the server** → signature check + bytecode verification + sandbox are P0 |
| Compiler | The core is the VM running `.e7b`; the compiler must also be able to run on phones (optional module) |
| Coroutines | No syntax in v1, but the VM architecture is designed to be "suspendable" from day one |
| Platforms | Android / iOS / Windows, C++17 |

---

## 1. Naming Survey

### 1.1 A discovery: `e7f` is an abbreviation of `everettjf`
`everettjf` = **e** + `verettj` (7 letters) + **f** → **e7f**, the same numeronym style as `i18n` (internationalization) and `k8s` (kubernetes).
The language name **E7** comes from e7f, and the engine gets the shorter name **e7vm** (a search found no project with the same name).

### 1.2 Candidate names

| Name | Relation to everettjf | Conflicts (search results) | Assessment |
|---|---|---|---|
| **E7** ⭐ | First half of the e7f abbreviation | No programming language with this name; only Berkeley's E7 course and Esterel v7 | Short, memorable, natural extensions (`.e7` / `.e7b`); recommended |
| **E7F** | The full numeronym | No conflicts found | Most distinctive, but a bit awkward to say; suits a project/organization name |
| E8 | No direct relation (the "next generation" of E7; mathematically, the E8 Lie group is beautiful) | A niche `e8vm` project and E8, a PSU course language | Loses the link to the ID |
| Eve | First three letters of Everett | The Eve language (witheve, abandoned in 2018), plus EVE Online | Many conflicts, not recommended |
| Evr / Ever | The "ever" in Everett | No language with this name found | Nice meaning (ever); a possible alternative |
| Rett | Second half of Everett | Same as the medical term Rett syndrome | Not recommended |
| Jet | j(e)t / jf | Many conflicts: JetBrains, the Microsoft Jet engine, etc. | Not recommended |

Note: "E" is already an old language (E by Electric Communities, 1997), hence "E7" rather than "E".

### 1.3 Naming scheme (final)
- Language: **E7** (pronounced "E-seven", from e7f = everettjf)
- Engine: **e7vm** (library `libe7vm`, optional compiler library `libe7c`)
- Repository: [github.com/e7lang/e7](https://github.com/e7lang/e7) (moved from `everettjf/e7fvm`)
- Source files `.e7`, bytecode `.e7b`, compiler command `e7c`, runner `e7`
- Future versions can naturally move on to **E8** (e.g. for a major release that introduces coroutines/generics)

---

## 2. Syntax Survey

### 2.1 Prior art for "statically typed scripting"

| Project | Approach | Takeaway for us |
|---|---|---|
| **Static TypeScript (Microsoft MakeCode, MPLR 2019 paper)** | A syntactic subset of TS where classes use **nominal typing**, compiled to ARM machine code with a mark-sweep GC; on object-access-heavy benchmarks it is ≥20x faster than MicroPython/Duktape and less than 2x slower than V8 | **The most direct precedent**: proves that "strict TS subset + static compilation" is feasible and fast |
| AssemblyScript | TS syntax + numeric types such as `i32/i64/f64` (made known to tsc through declaration files), compiled to Wasm | Declare extra numeric types in `.d.ts` and **get IDE support for free** |
| Swift / Kotlin | The benchmark for expressiveness in modern statically typed languages | Borrow nullable types `T?` and an expression-oriented style |
| Wasm / JVM | Bytecode type verification at load time | The security model for bytecode delivered from the server |

### 2.2 E7 = a strict TypeScript subset

Core rule: **every valid E7 program is also a valid TypeScript program** (not the other way round: E7 is stricter).

The same algorithm written in E7:

```ts
// Quicksort
function quicksort(a: number[], lo = 0, hi = a.length - 1): void {
  if (lo >= hi) return;
  const p = a[(lo + hi) >> 1];
  let i = lo, j = hi;
  while (i <= j) {
    while (a[i] < p) i++;
    while (a[j] > p) j--;
    if (i <= j) { [a[i], a[j]] = [a[j], a[i]]; i++; j--; }
  }
  quicksort(a, lo, j);
  quicksort(a, i, hi);
}

class Point {
  constructor(public x: number, public y: number) {}   // declares fields + constructor in one line
  dist(o: Point): number { return Math.hypot(this.x - o.x, this.y - o.y); }
}

const pts = [new Point(0, 0), new Point(3, 4)];
const total = pts.map(p => p.x + p.y).reduce((s, v) => s + v, 0);  // inferred as number
export function main(): string { return `total=${total}`; }
```

Why this choice:
1. **Tooling for free**: VS Code / WebStorm provide highlighting, completion, go-to-definition and type errors out of the box (all it takes is an `e7.d.ts` declaring the standard library and types such as `int`). Mobile and front-end developers have almost nothing to learn.
2. **Differential testing is possible**: run the same script on Node.js and the E7 VM and compare the output. This is a free and powerful way to test correctness.
3. **Static types come built in**: TS's type annotation syntax is mature (generics, nullable, unions, function types), so we don't have to design our own.
4. Elegance comes from: type inference, arrow functions, destructuring, template strings, `for...of`, constructor parameter properties, optional chaining `?.`, and `??`.

Cost: TS's structural typing, `any`, the prototype chain and so on must be cut (see §3).

### 2.3 Conclusion
- **Use a strict TS subset only**; Ruby-style syntax is no longer considered. Conciseness comes from type inference and a carefully chosen set of syntax features.
- Internally the compiler still keeps the "front end (syntax → typed AST) / back end (typed IR → bytecode)" split, which is standard engineering practice and makes testing and optimization easier.

---

## 3. E7 Language Specification Highlights (v1)

### 3.1 Type system
| Category | Contents | Notes |
|---|---|---|
| Primitive types | `boolean` `number` `string` `bigint` | Same semantics as TS; `number` = f64 |
| Fixed-width integers (E7 extension) | `i8 u8 i16 u16 i32 u32 i64 u64`, `int` = alias of `i64` | Real machine integers: wrapping overflow, bitwise operations at the type's width (see §3.8.1). Declared in `e7.d.ts` as aliases of `number`/`bigint`, so IDEs work as usual |
| Bytes | `Uint8Array`, `DataView` | The standard representation of byte strings; all signing input and output uses it |
| Arrays | `T[]`, `Float64Array`, `Int32Array` … | With static types, `number[]` **is itself contiguous f64 storage**, with no boxing |
| Containers | `Map<K,V>` `Set<T>` | Built-in generics |
| Classes | `class` (single inheritance, `implements`) | **Nominal typing**; field layout is fixed at compile time; methods dispatch through a vtable |
| Interfaces | `interface` | Structural checks at compile time, itable dispatch at runtime |
| Records | `type P = { x: number, y: number }` / object literals | Compiled to anonymous fixed-layout structs |
| Nullable | `T \| null`, `?.`, `??`, `!` | Only `null`; **no `undefined`** |
| Enums | `enum`, string literal unions `"a" \| "b"` | Compiled to integers |
| Functions | Function types, arrow functions, closures, default parameters, tuple returns `[int, string]` | |
| Generics | Generic functions and classes | Monomorphized per "value category" (one copy each for each integer width / number / reference) |
| Modules | `import` / `export` | Linked at compile time |

### 3.2 Numeric semantics (confirmed in v0.4)
v0.3 specified "integer literals default to `int`(i64)". Once it became clear the workload is signing algorithms, this was changed to:

- **Without type annotations, TS/JS semantics are followed exactly**: integer literals are `number`(f64); `& | ^ << >> >>>` convert to int32 first per JS rules; idioms such as `Math.imul`, `x >>> 0` and `(a + b) | 0` give exactly the same results as Node.
- **When annotated with a fixed-width integer type, machine integer semantics apply**: `let h: u32 = 0x6a09e667`; add/subtract/multiply wrap, bitwise operations are 32-bit, and `>>` is an arithmetic shift for signed types and a logical shift for unsigned types.

Reasons for the revision:
1. A large amount of existing JS/TS signing code (hashes, HMAC, custom obfuscation algorithms) relies heavily on JS bitwise semantics such as `|0` and `>>>0`. If integer literals defaulted to i64, such code would **compile in E7 but silently produce different results** (e.g. `1 << 31` is negative in JS but positive in i64; `x | 0` does not truncate in i64). For signing algorithms this is the most dangerous class of bug.
2. After the revision, code ported from JS gives the same results without changes; when performance matters, annotate the key variables as `u32`/`u64` to take the fast path.
3. The performance impact is small: in an interpreter, f64 add/subtract costs about the same as integer add/subtract, and the bottleneck is instruction dispatch; bitwise code that really needs to be fast will use fixed-width types explicitly.

Other rules are unchanged:
- Conversions between fixed-width integers and `number` must be explicit (`u32(x)`, `Number(h)`), as must conversions between different widths, ruling out implicit truncation.
- Integer division by zero is a runtime error; `number` division by zero yields Infinity/NaN per IEEE (same as JS).
- No fast-math, so floating-point results are identical across all three platforms (determinism).

### 3.3 Deliberately unsupported (relative to TS)
Free conversion via `any` / `unknown`, `undefined`, `==` (only `===` is allowed), `var`, the prototype chain/`prototype`, `eval`/`Function()`, adding or removing properties at runtime, `delete`, **`try`/`catch`/`finally`/`throw`**, getters/setters (v1), decorators, `with`, loose structural assignment conversions, and declaration merging.

### 3.4 Error handling (no exceptions)
- Expected failures are expressed through return values: `T | null`, or a tuple `[T, string | null]`.
- Runtime errors (array index out of bounds, null reference, integer division by zero, budget/memory limit exceeded, stack overflow) and script calls to `abort(msg)`: **the current call is terminated immediately**, `e7_call` returns an error code, and `e7_last_error` gives the message and a call stack with source line numbers. The VM stays usable, and the host can go on calling other functions.
- Benefit: the VM needs no exception unwinding tables; the generated bytecode and the verifier are both simpler.

### 3.5 Reserved keywords (for coroutines)
`function*` / `yield` and `async` / `await` are **recognized by the v1 parser but rejected as "not supported yet"**, so adding them later will not break existing code. `try`/`catch`/`throw` are likewise recognized and get a clear "E7 does not support exceptions" diagnostic.

### 3.6 Standard library (driven by the workload: math + strings)
| Module | v1 contents |
|---|---|
| `Math` | All of TS's `Math.*` (`sqrt` `pow` `exp` `log` `sin`… `floor` `ceil` `round` `trunc` `hypot` `min/max`); the host can inject the seed for `Math.random` for reproducibility; integer helpers `idiv` `imod` `gcd` `abs` `clamp` |
| Arrays | `push pop slice splice concat indexOf includes sort(cmp) reverse fill map filter reduce some every find join`; `Float64Array/Int32Array/Uint8Array` |
| Strings | `length charCodeAt at slice substring indexOf lastIndexOf includes startsWith endsWith split trim toUpperCase toLowerCase padStart padEnd repeat replace/replaceAll(string arguments)`, template strings |
| Conversion | `parseInt parseFloat Number() String() toFixed toString(radix)` |
| Containers | `Map` `Set` (insertion-ordered, same as JS) |
| Host | `print` (redirected to the host log), native functions registered by the host |

See also §3.8 for bytes, encoding and the `e7:crypto` module.

Not included for now: `RegExp` (to be added in v2 if needed), `Date` (time functions are provided by the host). `bigint` and `JSON` were moved up to v1 because signing needs them (see §3.8).

### 3.7 String implementation
- Semantics match TS: `length`/indexing count **UTF-16 code units**, so differential tests against Node give identical results.
- Dual storage representation (like V8): strings that are entirely Latin-1 use 1 byte per character, otherwise UTF-16; O(1) random access.
- Immutable + interned (short strings/constants), with the hash cached in the object header.
- Concatenation optimization: the compiler lowers `s += x` in loops and template strings to an internal StringBuilder, avoiding O(n²).
- Strings are exchanged with the host as UTF-8 (the C API layer converts).

---

### 3.8 Design for signing / signature verification

Signing scripts usually have two layers, and E7 optimizes each separately:

| Layer | Typical contents | Where it runs |
|---|---|---|
| **Protocol/business layer** | Parameter sorting and joining, canonical JSON, URL encoding, timestamps/nonces, Base64/Hex, choosing the algorithm, assembling signature headers | **E7 script** (can be updated flexibly by delivering it from the server) |
| **Cryptographic primitive layer** | SHA-1/2/3, MD5, HMAC, HKDF/PBKDF2, AES, RSA, ECDSA, Ed25519, Chinese national SM2/SM3/SM4, etc. | **Native C implementation**, exposed to scripts as a built-in module |
| **Custom algorithms** | Proprietary obfuscation/digest/transform algorithms | E7 script, on the fixed-width integer fast path |

Why: standard crypto algorithms usually run one to two orders of magnitude slower in an interpreter than natively (especially big-number arithmetic such as RSA/ECC), and native implementations are easier to make constant-time and get audited; the script only decides "how to combine them", which is exactly the part that needs to be delivered dynamically.

#### 3.8.1 Fixed-width integers and bitwise operations
- Types: `i8 u8 i16 u16 i32 u32 i64 u64` (`int` = `i64`).
- Operations: `+ - *` wrap; `/ %` are integer division; `& | ^ ~ << >> >>>` work at the type's width; the shift amount is taken modulo the width (unlike C's undefined behavior, E7 defines it explicitly).
- Intrinsics (each is a single VM instruction): `rotl32 rotr32 rotl64 rotr64 clz ctz popcnt bswap32 bswap64 mulhi64` (high 64 bits of a 64×64 product, for big numbers/hashing).
- Literals are inferred from context: in `const K: u32[] = [0x428a2f98, ...]` the literals are `u32` directly.

#### 3.8.2 Bytes and encoding
- `Uint8Array` is the single standard type for byte strings; it supports `subarray`, `slice`, `set` and concatenation via `concatBytes(...)`.
- `DataView`: `getUint32(off, littleEndian)` / `setUint32` etc. for endian-aware reads and writes (each is a single instruction).
- `TextEncoder` / `TextDecoder` (UTF-8).
- `e7:encoding` module: `hex`, `base64`, `base64url`, `base32` (encode/decode), `encodeURIComponent`.
- `JSON.stringify` (v1): deterministic output (field order = declaration order; optional key sorting), for "signing a JSON body"; `JSON.parse` returns a `JsonValue` type (with type-safe accessors, without introducing `any`).

#### 3.8.3 `bigint`
- Supports TS's `bigint` type and `123n` literals, backed by a native C++ big-number implementation.
- Also provides native functions such as `modPow`, `modInverse` and `gcd` (Montgomery multiplication), so scripts can implement non-standard number-theoretic algorithms.
- For standard RSA/ECC, calling `e7:crypto` directly is still recommended rather than hand-writing it in a script with `bigint`.

#### 3.8.4 Native `e7:crypto` module (synchronous API)
```ts
import { sha256, hmac, ed25519, ecdsa, rsa, timingSafeEqual } from "e7:crypto";

export function sign(params: Map<string, string>, key: KeyHandle): string {
  const keys = [...params.keys()].sort();
  const canonical = keys.map(k => `${k}=${encodeURIComponent(params.get(k)!)}`).join("&");
  const mac = hmac("sha256", key, new TextEncoder().encode(canonical));
  return hex.encode(mac);
}
```
- Algorithm coverage (trimmed by build flags to control size): digests SHA-1/224/256/384/512, SHA-3/Keccak, MD5, SM3, CRC32; MAC/KDF: HMAC, HKDF, PBKDF2; symmetric: AES-CBC/CTR/GCM, ChaCha20-Poly1305, SM4; signatures: RSA PKCS#1 v1.5 / PSS, ECDSA P-256/secp256k1, Ed25519, SM2.
- Implementation sources (candidates, undecided): BearSSL (MIT, constant-time, small, covers RSA/ECDSA/AES/SHA) + Monocypher (Ed25519/X25519/ChaCha) + GmSSL (Apache-2.0, Chinese national algorithms); or mbedTLS for everything. Writing crypto primitives from scratch is not recommended.
- WebCrypto's `crypto.subtle` is not used, because it is an asynchronous (Promise) API and v1 has no async.
- Node differential testing: provide an `e7:crypto` shim based on `node:crypto` and compare the output of the same script on Node and E7; also validate against official NIST / RFC test vectors.

#### 3.8.5 KeyHandle (keys never enter the script)
- The host stores keys in the iOS Keychain / Android Keystore / Windows DPAPI and passes only an opaque `KeyHandle` to the script.
- The script can use the handle for `hmac` / `sign`, but **cannot get the plaintext key**, so even if a delivered script is intercepted or tampered with, it cannot export the key.
- Keys can also be passed directly as `Uint8Array` (suitable for public keys used in signature verification, or for testing).
- MAC comparison must use `timingSafeEqual`.

#### 3.8.6 Performance targets (signing)
- Script layer: a typical "parameter canonicalization + HMAC-SHA256 + Hex" takes < 1 ms on a mid-range phone.
- A pure-script SHA-256 (on the `u32` fast path) serves as the benchmark for VM bitwise performance, with a target of 1/20 to 1/50 of a native implementation on the same device.
- To support this, the VM provides: fully typed fixed-width integer instructions, single-instruction `rotl/rotr`, single-instruction `Uint8Array`/`DataView` reads and writes, and superinstructions for common combinations (e.g. `x = rotr32(x, n) ^ y`).

## 4. Overall Architecture

```
 ┌──────────── Server / build side (main path) ─────┐
 .e7 → Lexer → Parser → AST → TypeChecker → typed IR → Optimizer → CodeGen
                                                  → .e7b → Ed25519 signing → deliver
 └──────────────────────────────────────────────────┘
 ┌──────────── Device side ─────────────────────────┐
 [optional] libe7c (same compiler source, build flag E7_WITH_COMPILER)
 .e7b → signature check → format parsing → **bytecode type verifier** → VM ⇄ GC
                                    ⇅ C API (exposes only host-authorized native functions)
 Swift/ObjC · Kotlin/JNI · C#/C++
 └──────────────────────────────────────────────────┘
```

- Production: **the server compiles and signs**, and devices load only `.e7b`. The core library `libe7vm` contains no compiler, so it is small and has a small attack surface.
- On-device compilation (debugging, local hot reload, offline editing): link `libe7c`; its output must still pass the verifier (the verifier trusts no source, including its own compiler).

---

## 5. VM Design (Redesigned for Static Types)

Static types let the VM be faster and simpler than dynamic VMs such as Lua:

### 5.1 Untagged registers + fully typed instructions
- Register slots are 8 bytes with **no type tag**; each function prototype carries a "register type table" (int / number / ref …).
- Instructions are type-specific: `ADD_I` `ADD_F` `LT_I_JMP` `LOAD_F64_ELEM` `GETFIELD off` `CALL_VIRT slot` `CALL_IFACE` …
- Field access is **a single memory read at a fixed offset**, with no hash lookup or inline cache; the quickening that dynamic VMs need is also unnecessary.
- Instruction encoding stays fixed-width 32-bit (`op:8 A:8 B:8 C:8` / `op:8 A:8 Bx:16`), with extended prefix instructions for very large functions.
- Dispatch: computed goto on GCC/Clang, falling back to switch on MSVC (clang-cl is recommended for Windows releases).

### 5.2 Bytecode verifier (the security foundation for server-delivered code)
Like the JVM/Wasm: at load time, each function gets a dataflow type check that guarantees:
- The types of each instruction's operand registers match what the instruction requires (e.g. an int cannot be dereferenced as a pointer);
- Constant/register/field/function indices are in bounds, and jump targets are valid;
- Call argument counts and types match.
Once verification passes, execution **needs no type checks at all**, and memory safety is guaranteed. Only the necessary runtime checks remain: array bounds, null pointers, integer division by zero.

### 5.3 Memory and GC
- Precise GC: register type table = GC root map; object layout table = pointer locations within objects.
- v1: stop-the-world mark-sweep; v2: incremental tri-color marking (to reduce jank on mobile).
- Each VM instance has its own heap, a configurable memory limit, and can plug in a custom allocator; instances share no state, so multiple threads can each run their own.
- String interning; `number[]` and similar are stored contiguously; large host arrays can be wrapped into scripts with **zero copy**.

### 5.4 Room for coroutines (no syntax exposed in v1)
1. **Script-to-script calls do not recurse on the C stack**: all script frames live on a VM-managed stack, and frames refer to each other by offsets rather than raw pointers (so the stack can grow and be saved as a whole).
2. **Execution state = a Fiber object** (stack + current frame + ip). v1 has only one main Fiber; when v2 adds `yield` / `await`, it simply creates more of them.
3. **The instruction budget = a natural suspension point**: the budget is checked at backward jumps and calls. When the budget runs out, v1 can already return `E7_SUSPENDED`, and the host calls `e7_resume` on the next frame to continue. So **running long algorithms across frames without blocking the UI** comes for free in v1, with no coroutine syntax needed.
4. Points where a native function calls back into the script are marked as "non-suspendable boundaries", with the rules fixed in advance.
5. The instruction set reserves opcode numbers for `YIELD` / `RESUME` / `AWAIT`.

### 5.5 `.e7b` file format
```
Header : magic "E7B\0" | format version | minimum VM version | flags | little-endian
Body   : constant pool | type table (class/interface/record layouts) | function prototypes (register type table + instructions) | import/export tables
Debug  : line number table, local variable names (strippable)
Trailer: SHA-256 digest | Ed25519 signature | public key ID
```

---

## 6. Security and Sandboxing (P0)

| Layer | Measures |
|---|---|
| Transport/origin | `.e7b` signature (Ed25519); the host has a built-in public key and refuses to load on signature verification failure; key rotation is supported (public key ID) |
| Structure | All format parsing is bounds-checked; the parser and verifier are **fuzzed** (libFuzzer) |
| Types | Bytecode verifier (§5.2) |
| Capabilities | No file/network/system calls by default; scripts can only call native functions explicitly registered by the host (allowlist) |
| Resources | Instruction budget, memory limit, call-depth limit, wall-clock timeout; on exceeding a limit, the script terminates safely and returns an error |
| Versioning | The VM refuses to load bytecode newer than it supports; the server delivers the build matching the client's VM version |
| Compliance | No JIT on iOS; pure interpretation, and delivered scripts do not change the app's primary purpose |
| Hardening (optional, v1.x) | If the signing algorithm itself must resist reverse engineering: randomly shuffle opcode numbers per release, encrypt the constant pool/strings, strip debug info, flatten control flow; the VM checks its own integrity. Note: client-side algorithms can never be kept absolutely secret; hardening only raises the cost of reverse engineering |

---

## 7. Embedding API (Draft C Interface)

```c
e7_vm*  e7_new(const e7_config* cfg);   // memory limit, instruction budget, allocator, trusted public keys
void    e7_free(e7_vm*);
int     e7_load(e7_vm*, const uint8_t* e7b, size_t len, e7_module** out);  // signature verification + bytecode verification
int     e7_compile(const char* src, size_t len, e7_buffer* out, e7_diag* diag); // requires libe7c
int     e7_call(e7_vm*, e7_module*, const char* fn, const e7_value* args, int argc, e7_value* ret);
int     e7_resume(e7_vm*, e7_value* ret);          // continue after the budget runs out
void    e7_register_native(e7_vm*, const char* name, const char* signature, e7_native_fn, void* ud);
e7_value e7_wrap_f64_array(e7_vm*, double* data, size_t n, e7_release_fn release);
const char* e7_last_error(e7_vm*);                 // call stack with source line numbers
```
Native functions are registered with a type signature (e.g. `"(number[],int)->number"`), and script calls are checked against it at both compile time and load time.
Higher-level wrappers: iOS → Swift Package + xcframework; Android → AAR (JNI + Kotlin); Windows → DLL / C# P/Invoke.

---

## 8. Toolchain
- `e7.d.ts`: declarations for the standard library and types such as `int`/`i32`, giving VS Code native support for E7.
- `e7c`: the compiler CLI (`--check` for type checking only, `-O2`, `--strip-debug`, `--sign key`).
- `e7`: runner / disassembler (`e7 dis a.e7b`) / benchmark tool.
- Testing: unit tests + `.e7` cases (with expected output) + **Node.js differential testing** + fuzzing.
- Benchmarks: numeric (fib, nbody, spectral-norm, mandelbrot, matmul, sieve, quicksort) + strings (concatenation, split/join, find and replace, word frequency with Map<string,int>); compared against Lua 5.4, QuickJS and Node (interpreter mode `--jitless`).

## 9. Directory Layout (Planned)
```
include/e7/e7.h       public C API
src/common/           memory, strings, containers
src/compiler/         lexer parser ast checker ir optimizer codegen   → libe7c
src/vm/               loader verifier interp gc object fiber natives  → libe7vm
src/stdlib/           Math String Array Map etc.
tools/e7c tools/e7    command-line tools
lib/e7.d.ts           IDE type declarations
tests/ bench/ fuzz/
platform/ios android windows
```

## 10. Milestones
| Phase | Contents | Acceptance |
|---|---|---|
| M0 Spec | Freeze the v1 syntax subset, type rules, numeric semantics, instruction set and `.e7b` format | This document finalized + `e7.d.ts` |
| M1 Front end | Lexer, Parser (TS subset), AST, diagnostics | Example scripts parse; invalid syntax is reported accurately |
| M2 Type checking | Inference, class/interface/generic/nullable checks | Cases agree with tsc (with dedicated cases where E7 is stricter) |
| M3 Back end + VM | IR, register allocation, CodeGen, interpreter, primitive types and functions | fib / loops / quicksort run; Node differential tests pass |
| M4 Objects and GC | class/interface/closures/arrays/Map/strings, mark-sweep | Comprehensive cases pass with no leaks (ASan/LSan) |
| M5 Bytecode and security | `.e7b` serialization, verifier, signing, sandbox, budget suspend/resume | 24h of fuzzing with no crashes; tampered bytecode is rejected |
| M5.5 Signing capabilities | Fixed-width integer instructions, `Uint8Array`/`DataView`, `e7:encoding`, `e7:crypto` (wired to native libraries), `bigint`, KeyHandle | All algorithms pass NIST/RFC test vectors; differential results match the Node shim |
| M6 Performance | Constant folding, fused compare-and-jump, inlining small functions, superinstructions | Benchmarks match or beat Lua 5.4; script SHA-256 meets the §3.8.6 target |
| M7 Three-platform integration | C API, iOS/Android/Windows builds and demo apps, on-device compilation | The same `.e7b` gives identical results on real devices on all three platforms |
| E8 (v2) | Coroutines (`yield`/`async`), incremental GC, getters/setters, RegExp | — |

## 11. Decision Log
| Question | Decision |
|---|---|
| Naming | Language E7, engine e7vm |
| Syntax | Strict TS subset only |
| Integer default | **Same as JS**: `number` when unannotated; fixed-width integers such as `u32`/`u64` are annotated explicitly (confirmed in v0.4, see §3.2) |
| Exceptions | Not supported; runtime errors terminate the call and return to the host |
| Workload | Mainly signing / signature verification, plus math and strings |
| Crypto primitives | Native implementations exposed as `e7:crypto`; scripts handle the composition logic |

## 12. Next Steps (Deliverables for the M0 Spec Freeze)
1. `docs/SPEC.md`: E7 syntax subset (EBNF), type rules, numeric and string semantics, error model.
2. `docs/BYTECODE.md`: instruction set table, encoding, `.e7b` format, verification rules.
3. `lib/e7.d.ts`: declarations for the standard library and types such as `int`, so E7 scripts can be tried out directly in VS Code.
4. `examples/`: about 10 typical scripts that double as the first tests and benchmarks: API parameter signing (HMAC), JSON body signing, RSA/ECDSA/Ed25519 signature verification, pure-script SHA-256, a custom bitwise digest, Base64/Hex, big-number modular exponentiation, and a few numeric and string algorithms.

### Open questions
1. Which specific algorithms are needed? In particular, are the **Chinese national SM2/SM3/SM4** algorithms and secp256k1 needed?
2. Does the signing algorithm itself need **anti-reverse-engineering hardening** (the hardening row in §6)?
3. Are keys managed by the host via Keychain/Keystore and passed to scripts as handles?

## References
- Static TypeScript paper (MPLR 2019): https://mmoskal.github.io/pdf/static-typescript.pdf
- Rocket-fast embedded TypeScript for MakeCode Arcade: https://www.microsoft.com/en-us/research/blog/rocket-fast-embedded-typescript-for-makecode-arcade/
- MakeCode language notes: https://makecode.com/language
- The E language (naming conflict reference): https://en.wikipedia.org/wiki/E_(programming_language)
- The Eve language (witheve, naming conflict reference): https://github.com/witheve/Eve
- e8vm (naming conflict reference): https://github.com/ryanphuang/e8
- BearSSL: https://bearssl.org/
- Monocypher: https://monocypher.org/
- GmSSL (Chinese national algorithms): https://github.com/guanzhi/GmSSL
- Mbed TLS: https://github.com/Mbed-TLS/mbedtls
