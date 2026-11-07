# E7 design decisions

This document records why E7 and e7vm are built the way they are: the foundations (sections 1-6), then the decisions
behind coroutines, `async` / `await`, the bytecode format, leaving out regular expressions, literal types, recursive
types and properties that hold `undefined` (D1-D7), with notes on how each was implemented. The specifications are
[SPEC.md](SPEC.md) (the language), [BYTECODE.md](BYTECODE.md) (the `.e7b` format and the verifier) and
[EMBEDDING.md](EMBEDDING.md) (the C API); where this document and those differ, they are authoritative.

Every decision keeps the rules of [SPEC.md](SPEC.md) section 0.1: types are known at compile time (the fixed rule),
the bytecode is verifiable, and behaviour is identical to JavaScript as Node.js 22 implements it.

| Item | Decision |
|---|---|
| Types | Strict static typing, no `any` |
| Syntax | A strict TypeScript subset: every E7 program is a valid TypeScript program |
| Names | Language **E7**, engine **e7vm** |
| Numbers | Without annotations, JavaScript semantics (`number` is f64); fixed-width integer types (`u32`, `i64`, ...) when annotated |
| Errors | `try` / `catch` for the errors Node.js also throws; E7-specific checks and resource limits end the call |
| Workload | Mainly signing and signature verification, plus general math and string processing |
| Script source | Delivered from a server: signature check, bytecode verification and a sandbox are required |
| Compiler | The core is the VM running `.e7b`; the compiler is an optional part of the library |
| Platforms | Android, iOS, macOS, Windows, Linux and WebAssembly, from one C++17 code base |

---

## 1. Names

`everettjf` is **e** + 7 letters + **f**, the numeronym style of `i18n` and `k8s`: **e7f**. The language takes the
first half, **E7**; the engine is **e7vm** (library `libe7vm`). Source files are `.ts` (or `.e7`), compiled modules
`.e7b`, the compiler `e7c` and the runner `e7`. "E" alone is an older language (E, Electric Communities, 1997), hence
E7.

## 2. A strict TypeScript subset

Core rule: **every valid E7 program is also a valid TypeScript program**, and runs with the same result on a
JavaScript engine. E7 is stricter than TypeScript, not different from it.

| Prior art | Approach | What E7 takes from it |
|---|---|---|
| Static TypeScript (Microsoft MakeCode, MPLR 2019) | A syntactic subset of TypeScript with nominal classes, compiled ahead of time | Proof that a strict TypeScript subset compiles to fast code |
| AssemblyScript | TypeScript syntax plus numeric types (`i32`, `i64`, ...) declared for tsc in `.d.ts` files | Extra numeric types through a declaration file, so editors understand them |
| Wasm, the JVM | Bytecode type verification at load time | The security model for code delivered from a server |

Why a TypeScript subset rather than a new syntax:

1. **Tooling for free.** Editors highlight, complete and type-check E7 with no plugin; `lib/e7.d.ts` declares the
   built-in modules and the integer types. Mobile and web developers have little to learn.
2. **Differential testing.** The same program runs on Node.js and on e7vm; the outputs must be identical. The test
   suite and a random program fuzzer compare them byte for byte.
3. **A mature type syntax.** Generics, nullable types, function types and literal types exist already.

The cost is that TypeScript's dynamic parts are left out: `any`, unions of different representations, index
signatures, the prototype chain, `eval` and anything else whose representation would depend on a value at run time
(SPEC.md lists them; [TYPESCRIPT.md](TYPESCRIPT.md) gives the reason and the alternative for each).

## 3. Numbers

- **Without type annotations, JavaScript semantics apply exactly.** Integer literals are `number` (f64); the bitwise
  operators convert to int32 first, so idioms such as `Math.imul`, `x >>> 0` and `(a + b) | 0` give the same results
  as Node.js. A large amount of existing signing code relies on them; giving integer literals another default would
  make such code compile and silently produce different results.
- **Annotated fixed-width integers use machine semantics.** `let h: u32 = 0x6a09e667`: `+ - *` wrap, bitwise
  operations work at the type's width, `>>` is arithmetic for signed and logical for unsigned types, and the shift
  amount is taken modulo the width. Code that needs speed annotates its hot variables (`u32`, `u64`).
- Conversions between integer types and `number`, and between widths, are explicit (`u32(x)`, `Number(h)`). Integer
  division by zero is a run-time error; `number` division follows IEEE 754. There is no fast-math, so results are
  identical on every platform.

## 4. Designed for signing

Signing scripts have two layers, and E7 treats them differently:

| Layer | Typical contents | Where it runs |
|---|---|---|
| Protocol | Sorting and joining parameters, canonical JSON, URL encoding, timestamps and nonces, Base64 / hex, choosing the algorithm, building headers | The E7 script, updated by delivering a new module |
| Cryptographic primitives | SHA-256/384/512, HMAC, HKDF, PBKDF2, AES-GCM, ChaCha20-Poly1305, Ed25519, X25519 | Native code, exposed as `e7:crypto` |
| Custom algorithms | Proprietary digests and transforms | The E7 script, on the fixed-width integer fast path |

Standard primitives are one to two orders of magnitude slower in an interpreter, and native implementations are
easier to make constant-time and to audit; the script decides how to combine them, which is the part that changes.
The primitives come from BearSSL (SHA-2, HMAC, AES-GCM, ChaCha20-Poly1305) and Monocypher (Ed25519, X25519);
HKDF and PBKDF2 are E7's own code on top of BearSSL's HMAC. All are checked against Node.js `node:crypto` and
published test vectors.

- **Only modern algorithms.** `e7:crypto` has no MD5, SHA-1, SHA-3 / Keccak, AES-CBC / CTR, RSA or ECDSA, and no
  Chinese national algorithms (SM2/3/4). A smaller set is less to audit, fuzz and keep constant-time, and new
  protocols do not need the rest. An app that must speak a legacy protocol registers a host function for that
  algorithm (backed by the platform's own crypto library), so the legacy code stays in the host.

- **Bytes.** `Uint8Array` is the one byte-string type; `DataView` gives endian-aware reads and writes;
  `e7:encoding` has hex, Base64, Base64url, Base32, PEM and CRC-32.
- **`bigint`** is backed by a native arbitrary-precision implementation with JavaScript's semantics.
- **Keys never enter the script.** The host keeps keys in the Keychain, Keystore or DPAPI and passes an opaque
  `KeyHandle`: a script can use it for `hmac` or `sign` but cannot read the key. MACs are compared with
  `timingSafeEqual`.

## 5. Architecture and the VM

```
 Server / build side
   .ts → lexer → parser → type checker → code generator → .e7b → Ed25519 signature → delivery
 Device
   .e7b → signature check → format parsing → bytecode verifier → interpreter ⇄ GC
            ⇅ C API (only the native functions the host registers)
   Swift · Kotlin / Java · C# · C / C++ · JavaScript (WebAssembly)
```

In production the server compiles and signs, and devices load only `.e7b`. The compiler is an optional part of the
library (`E7_WITH_COMPILER`); its output passes the same verifier as any other module, since the verifier trusts no
source.

Static types let the VM be simpler and faster than a dynamically typed one:

- **Untagged registers, typed instructions.** A register slot is 8 bytes without a type tag; every function carries
  the kind of each register (integer, float, reference). Instructions are specific to their operand kinds
  (`ADD_I`, `ADD_F`, ...), emitted directly from the static types, so no run-time quickening is needed. Instructions
  are fixed-width 64-bit words (`op:16 A:16 B:16 C:16`, up to 65535 registers); compare-and-jump fusion, constant
  register hoisting and loop inversion are done by the compiler. Dispatch uses computed goto where the compiler
  supports it.
- **The verifier is the security foundation.** At load time every function gets a data-flow check: operand kinds
  match each instruction, indices and jump targets are in range, calls match their signatures. A module that passes
  needs no type checks while it runs; only bounds, null and division checks remain. ([BYTECODE.md](BYTECODE.md)
  section 3.)
- **Memory.** A precise mark-sweep collector: register kinds are the root map, class layouts give the reference
  fields. Each VM has its own heap and memory limit and shares no state, so threads can each run their own VM.
  Strings are stored as UTF-16, as JavaScript sees them; concatenation in loops is kept linear by the runtime.
- **Frames on the VM stack.** Script-to-script calls do not recurse on the C stack: frames live on a VM-managed stack
  and refer to each other by offsets. The instruction budget is checked at backward jumps and calls, so a call can
  stop with `E7_SUSPENDED` when its budget runs out and continue with `e7_resume`. The same property makes
  coroutines cheap (D1).

## 6. Security and sandboxing

| Layer | Measure |
|---|---|
| Origin | Ed25519 module signatures; the host configures trusted public keys and rejects anything else (key ids allow rotation) |
| Structure | Every read of the format is bounds-checked; the loader, verifier and compiler are fuzzed |
| Types | The bytecode verifier (section 5) |
| Capabilities | No file, network or system access; scripts call only the native functions the host registers |
| Resources | Instruction budget, memory limit, call-depth limit; exceeding one ends the call cleanly with an error |
| Versions | A module's header says which e7vm it needs (`requires`); an older VM rejects it with `E7_ERR_VERSION` (D3) |
| Platform rules | No JIT, pure interpretation (allowed on iOS) |

The security reviews and their results are in [PRODUCTION.md](PRODUCTION.md).

---

| | Decision | VM change |
|---|---|---|
| D1 | Coroutines: a generator or `async` activation keeps its frame in a heap object between resumptions | yes |
| D2 | `async` / `await`: ECMAScript promise jobs in the VM, the host as the event loop | yes |
| D3 | One bytecode format: function flags and class flags are part of it | yes |
| D4 | No regular expressions: left out to keep the runtime small | no |
| D5 | Literal types: checked at compile time, represented as their base type | no |
| D6 | Recursive interfaces and type aliases | no (JSON descriptors gain a back-reference) |
| D7 | Properties that hold `undefined`: a presence mask in objects of interface / object types | yes |

## D1. Coroutines

### Decision

A generator or `async` function call creates a **coroutine object** (`CoroObj`) that owns the function's frame while
it is not running. Suspending (`yield`, `await`) copies the frame's registers and `ip` into the coroutine and pops
the frame; resuming pushes a frame on top of the current stack and copies them back. While it runs, a coroutine's
frame is an ordinary frame on the VM stack.

### Why this works

- Script frames live on the VM stack, addressed by offsets (`Frame::base`), not on the C stack (section 5). Moving
  one frame is a copy of `nregs` slots.
- `yield` and `await` may appear only directly in the body of their own generator or `async` function (an ECMAScript
  rule, enforced by tsc). When one runs, the coroutine's frame is the **top** frame: every call it made has
  returned. So suspending moves exactly one frame, never a stack segment, and never crosses a native frame.
- Closures capture cells, not registers (cells are heap objects), so a moved frame does not invalidate any closure.
- Register kinds are fixed per function, so the saved registers are scanned precisely by the collector with the
  function's own kind table, as frames are.

### Semantics

- **Generators.** The first instruction of a generator function is `GEN_START`: it creates the coroutine, saves the
  frame (with the arguments already in their registers) and returns the coroutine to the caller as the call's
  result, without running the body (as in JavaScript). `RESUME` (used by `next`, `return`, `throw` and by `for...of`)
  pushes the frame back and continues after the `yield` or at the start. `yield` (`YIELD`) saves the frame and returns
  the value to the resumer with `done = false`; returning from the function marks the coroutine finished and returns
  the value with `done = true`.
- **How `return()` and `throw()` reach the body.** `YIELD` writes a resumption mode into an integer register: 0 for
  `next(v)`, 2 for `return(v)`. For mode 2 the compiler emits, after each `yield`, the same code as a `return`
  statement at that point, so `finally` blocks run through the handler-table mechanism; the VM has no separate
  unwinding path. `throw(e)` raises `e` at the `YIELD` instruction, where the handler tables of the generator function
  apply as for any `throw`.
- **Re-entrancy.** Resuming a coroutine that is running raises `TypeError: Generator is already running`, as in
  JavaScript. Resuming a finished one returns `done = true` (`next`), its value (`return(v)`), or raises (`throw`).
- **No allocation in loops.** `RESUME` writes the value and the done flag into two registers; `for...of`, spread and
  destructuring over a generator therefore allocate no `{ value, done }` objects. Only an explicit `g.next()` in
  script code builds the result object.
- **Budget suspension** (`suspend_on_budget`) works inside a running coroutine: its frame is on the stack like any
  other.
- **Natives** never suspend halfway. A generator resumed from a callback that a native made (a `sort` comparator
  calling `g.next()`) is fine: its frame is pushed above the callback's frame and popped again at the `yield`.

### Iterators

- `Generator<T, TReturn, TNext>`, `Iterator<T>`, `IterableIterator<T>` and `IteratorResult<T>` are built-in types.
  `IteratorResult<T, TReturn>` is the record `{ done: boolean; value: T | TReturn }`, accepted when `TReturn` is `T`,
  `void` or `undefined` (then `value` is `T | undefined`, a nullable type); any other `TReturn` is a compile error,
  because `value` would be a general union.
- A class with a method named `[Symbol.iterator]` is iterable: `Symbol.iterator` is accepted as this one fixed method
  name, not as a value (`Symbol` values stay out). `for...of`, spread and array destructuring over it call the method
  and then `next()`, and call `return()` when the loop exits early, as the specification requires.

### Costs and limits

- A coroutine holds `nregs` slots plus a header; it is charged to the memory limit like any object.
- `RESUME` / `YIELD` copy `nregs` slots each way. Functions with very many registers pay for it; acceptable for the
  target workloads (measured in the benchmarks).
- Debugger: a suspended coroutine is not on the stack, so it has no frame to show; stepping over `yield` / `await`
  stops at the next statement of the same coroutine when it resumes (tracked by coroutine identity).

---

## D2. `async` / `await`

### Decision

The VM implements the ECMAScript promise machinery itself: promise objects, reaction records, a **microtask queue**
processed in the order the specification gives, and `await` as a coroutine suspension (D1) with the one-tick
semantics of ES2019 and later. There is **no event loop and no timer** in the VM: the host is the event loop.

### The host as the event loop

```ts
declare function fetchKey(id: string): Promise<Uint8Array>;   // a host function that completes later

export async function sign(id: string, body: Uint8Array): Promise<string> {
  const key = await fetchKey(id);
  return hexEncode(hmac("sha256", key, body));
}
```

- The host registers `fetchKey` with `e7_register_async_native`. When the script calls it, the VM creates a pending
  promise and an integer **ticket**, and calls the host function with the arguments and the ticket. The host may
  settle the ticket at once or later, with `e7_settle(vm, ticket, &value)` or `e7_settle_error(vm, ticket, message)`.
- `e7_call` runs the script until the VM stack is empty and the microtask queue is drained. If no ticket is open, it
  returns the result (an `async` function's settled promise gives its value; a rejection is reported like an
  uncaught error). Otherwise it returns **`E7_PENDING`**. The host settles one or more tickets and calls
  `e7_resume`, which drains the microtasks the settlements queued and returns `E7_OK`, an error, or `E7_PENDING`
  again.
- While a call is pending, no new `e7_call` can start (`E7_ERR_STATE`, as for a suspended call);
  `e7_abort_suspended` abandons it, and later settlements of its tickets return `E7_ERR_STATE`.
- All of this happens on the VM's thread, like every other call; a host that completes work on another thread
  marshals the settlement back.
- An exported `async` function returns `Promise<T>`; the host sees `T` (the export's host signature marks it).
- Unhandled rejections: a promise rejected with no handler when the queue is drained fails the call with the error,
  as Node.js ends the process with it.

### Why not an event loop in the VM

E7 runs inside apps that already have one (Android's, iOS's, a server's). A second loop would need threads or timers
inside the VM, which the sandbox does not allow and which would make results depend on timing. With the host as the
loop, a script's behaviour depends only on the order in which the host settles tickets.

### Ordering identical to Node.js

The microtask queue follows the specification's jobs (`PromiseReactionJob`, `PromiseResolveThenableJob`), including
the extra ticks for resolving a promise with a promise and the single tick of `await` on a native promise. The
differential tests check it: the `e7` CLI and the Node.js shim provide the same test-only async host functions, and
both settle open tickets one at a time in creation order, each in its own macrotask (`setImmediate` in Node.js,
`e7_settle` + `e7_resume` in the CLI). The fuzzer also settles them in random but seeded orders.

### Types (the fixed rule)

- `Promise<T>` is a built-in generic type with a fixed representation (a reference).
- `then` takes a callback returning `U` or `Promise<U>`; which one is decided by the callback's **static** return
  type, so there is no union at run time. The same for `resolve(value)` and `await value` (`await` of a non-promise
  value is compiled as `await Promise.resolve(value)`, with the same number of ticks as in JavaScript).
- `Promise.all` / `allSettled` / `race` / `any` take an array of `Promise<T>` with one `T` (tuples of different types
  would need a union result); `allSettled` gives `PromiseSettledResult<T>[]`, a record with `status`, `value?` and
  `reason?`.
- `new Promise<T>((resolve, reject) => ...)` is supported: `resolve` and `reject` are closures over the promise.

### In and out of scope

In: `async` functions, methods and arrow functions, `await` (also top-level in the entry module), `Promise<T>` with
`then` / `catch` / `finally`, `Promise.resolve` / `reject` / `all` / `allSettled` / `race` / `any`, `new Promise`,
async generators and `for await`, `AggregateError`. Out: timers (`setTimeout`, `setInterval`), `queueMicrotask`,
`Promise.withResolvers`, and thenables that are not promises (a class with a `then` method is not awaited
specially; tsc types it as a plain object, and so does E7).

### Bindings

Each language binding wraps `E7_PENDING` in its own model: Kotlin `suspend` functions, Swift `async` with
continuations, C# `Task<T>`, JavaScript `Promise`. Host async functions are registered with the language's async
function type.

---

## D3. One bytecode format

### Decision

There is one `.e7b` format ([BYTECODE.md](BYTECODE.md)), with the format identifier 3 in its header. The features
of D1 and D7 are part of its layout:

- a function flags byte (generator, `async`, async generator, and the value kinds of the coroutine), checked by the
  verifier: `GEN_START`, `YIELD` and `AWAIT` appear only in functions with the matching kind, `GEN_START` /
  `ASYNC_START` only as the first instruction;
- a class flags byte (records with a presence mask, D7; records with accessors);
- the opcodes `GEN_START`, `ASYNC_START`, `YIELD`, `RESUME_I/F/R`, `AWAIT` and `FIELD_ABSENT`; promise and iterator
  operations are natives, so they need no verification rules of their own.

A loader accepts exactly this format identifier and rejects any other with `E7_ERR_VERSION` before it reads the
body, so a file of another layout is never mis-parsed.

### The `requires` field

The header also holds `requires`: the oldest e7vm that can run the module. The compiler computes it from "since"
tables that give, for each opcode and native function, the release that introduced it (`src/core/since.h`). In 0.1.0
the tables are empty, so every module requires 0.1.0. A later release that adds an opcode or a native adds an entry,
and only the modules that use it require that release; a VM rejects a module whose `requires` is newer than itself
with `E7_ERR_VERSION` instead of failing in the verifier. Hosts read the field without loading the module
(`e7_inspect`, `e7c --info`), and update manifests carry it as `minVm` ([DEPLOYMENT.md](DEPLOYMENT.md)).

### Why flags in the layout rather than more opcodes

The kind of a function and the representation of a class change how the verifier and the VM treat every
instruction that touches them; they belong in the descriptors the verifier reads first, not in instructions found
later. New opcodes and natives are handled by `requires`; the format identifier changes only when the layout itself
changes.

---

## D4. No regular expressions

### Decision

E7 has no regular expressions. A regular expression literal (`/.../`), `RegExp`, `RegExpMatchArray`,
`RegExpExecArray` and the String methods that only take a pattern (`match`, `matchAll`, `search`) are compile
errors that point to the string methods. `split`, `replace` and `replaceAll` take string patterns only.

### Why

e7vm is embedded in apps, and every app pays for every feature in its download size. An ECMAScript regular
expression engine that matches V8 needs its own parser, compiler and backtracking matcher plus Unicode property and
case-folding tables: about 160 KB of the 1.5 MB Android runtime, more than the VM's interpreter. The business logic
E7 is built for (rules, validation, request signing, data shaping) rarely needs patterns that `indexOf`,
`startsWith`, `split` and `replaceAll` cannot express, and leaving the engine out also removes a class of
denial-of-service inputs (catastrophic backtracking) and a large part of the security review surface.

A host that needs pattern matching can provide it as a host function (`declare function matches(pattern: string,
s: string): boolean`), with the platform's own engine.

---

## D5. Literal types

### Decision

String, numeric, boolean and bigint literal types (`"GET"`, `0`, `true`), unions of literals of **one base type**
(`"GET" | "POST"`, `0 | 1 | 2`), with `null` / `undefined`, and enum member types (`E.A`) are checked at compile time
as tsc checks them, and represented at run time as their base type. A literal type is a refinement of its base type,
so the fixed rule holds: the representation never depends on a value.

- Narrowing by `===` / `!==` and `switch` on literal unions, with exhaustiveness (`never`) as for enums.
- `as const` on arrays and objects of literals gives `readonly` tuples and objects of literal types.
- Unions of different base types (`string | number`, `"a" | 1`) and unions of object types (discriminated unions)
  stay out: their representation would depend on the value.

`let m: "GET" | "POST" = s` with a `string` `s` is a compile error, as in tsc.

---

## D6. Recursive interfaces and type aliases

### Decision

Named object types are created before their members are resolved, so a member may refer to the type itself or to
types that refer back (`interface Tree { label: string; children: Tree[] }`, mutually recursive interfaces, generic
`interface List<T> { head: T; tail: List<T> | null }`).

- A recursive **type alias** of an object type (`type Tree = { children: Tree[] }`) is resolved like an interface: a
  named (nominal) shape instead of an interned anonymous one, because interning by structure cannot describe a cycle.
  Assignability between such types stays structural, with a cycle guard (two types are compatible if they are
  compatible assuming the pair being compared already is).
- Generic instances are registered before their members are instantiated, so `List<number>` refers to itself.
- Type names in messages, JSON descriptors and the debugger stop at a cycle.
- **`JSON.parse(text) as Tree`**: the JSON type descriptor (a string the VM interprets) has a back-reference to an
  enclosing record (`@<depth>`).

No other VM change: objects of recursive types are ordinary records.

---

## D7. Properties that hold `undefined` (a presence mask)

### Decision

Records whose type has optional properties or properties whose type includes `undefined` get a **presence mask**:
one bit per field after the fields (one 64-bit word per 64 fields), marked in the class descriptor's flags. A new
record has every bit set; `FIELD_ABSENT` clears the bit of a property that an object literal or a spread leaves out,
and any assignment to the property sets it again. Reading a field is unchanged (an absent field reads as
`undefined`), so property reads cost nothing extra.

`Object.keys` / `entries` / `values`, spread, `Object.assign`, `JSON.stringify` and `console.log` consult the mask:
a property is listed when its bit is set, also when it holds `undefined`, as in JavaScript. A record without a mask
has no property that could be absent, so all its fields are own properties.

### Why a mask and not an "absent" value

An "absent" sentinel stored in the field would have to be translated to `undefined` on every read of every reference
field, a cost on the hottest path. The mask costs one bit per field and a store on writes to the flagged records only
(`SETFIELD` on a class with the flag; the class is known from the object, so the check is one branch).

### Effect

With `exactOptionalPropertyTypes` and the mask, a record behaves like a JavaScript object for every property its type
declares.

---

## Implementation notes

- **D1.** The value kinds in the function flags are 0 none, 1 `I`, 2 `F`, 3 `R` (a plain function has flags 0), and
  bits 6-7 hold the kind of the body's return value. `GEN_START` is the first instruction, so default parameter
  values of a generator are evaluated when the body first runs (at the first `next()`), not at the call. Iterators
  are generators at run time: an iterator object written by hand (an object with a `next()` method) is not an
  `Iterator<T>`; a class is iterable through a generator method `*[Symbol.iterator]()`. `next()` on a finished
  generator whose return type is a primitive gives NaN / 0 / false instead of `undefined` (the value's
  representation is fixed).
- **D2.** The C API is `e7_register_async_native`, `e7_settle`, `e7_settle_error`, `e7_open_tickets` and the status
  `E7_PENDING` (15). A call returns `E7_PENDING` whenever host tickets are open at its end, also when the entry
  function is not `async` (the call is finished when the work it started is). An uncaught error of the call itself
  runs the jobs it queued first (as JavaScript does), then ends the call. For tests, `e7:test` provides `later(v)`
  and `laterReject(e)`: promises the VM settles itself, one per turn, in creation order (`setImmediate` in the
  Node.js shim).
- **Async generators** (`async function*`) are coroutines of function kind 3 that start like a generator
  (`GEN_START`); their `next` / `throw` / `return` requests are `RESUME_R` with an "async request" bit in its extra
  word (BYTECODE.md section 6.2). The VM keeps the request queue of the specification in the coroutine
  (AsyncGeneratorEnqueue, Resume, Yield, CompleteStep, DrainQueue, AwaitReturn): a request gets a promise at once and
  runs the body only when the generator waits at its start or at a `yield`; `YIELD` in an async generator answers the
  first request and, when another is queued, continues without suspending; `RET_*` and an error leaving the frame
  answer the current request and drain the queue. The compiler emits the awaits around them (`yield v` and
  `return v` await `v`, `return(v)` at a `yield` awaits `v` before returning, `for await` awaits each request's
  promise), so the number of promise jobs is the specification's and the order of callbacks is Node's. The
  IteratorResult records are built by the VM, of the record class the request names. `yield*` returns the inner
  iterator's value after `return(v)` without awaiting it again (the current specification and V8). `yield*` of a
  sync iterable (an array, a generator, an iterable class) is compiled as the specification's async-from-sync
  iterator, step by step: the sync iterator is called at once, its value goes through `Promise.resolve` and a `then`
  (whose rejection handler is a compiler-made closure that calls the generator's `return()`, as `closeIterator`
  does) and the outer await, so a step takes the same two jobs as in Node.js; `throw(e)` into an array (no `throw`
  method) rejects with a `TypeError` a job later, and `return(v)` on it gives `{ value: v, done: true }` (the ES2025
  text, also what Node.js 22 and 24 do).
- **D4.** The parser still recognizes a regular expression literal, so that `/a+/` is reported as "regular
  expressions are not supported in E7" with a hint rather than as a confusing division.
- **D5.** `as const` works for literals and for arrays and objects of literals; a `const` initialized with a literal
  has the literal's type, a `let` the base type (TypeScript's widening). An enum member used as a type (`kind: E.A`)
  is the enum's type (tsc checks the member).
- **D6.** The back-reference in a JSON descriptor is `@<n>` (the n-th enclosing object descriptor), read by the
  ordinary `JSON.parse` / `JSON.tryParse` natives.
- **D7.** Records whose type has a property that may hold `undefined` (optional or `| undefined`) carry the mask;
  object literals, spread and `JSON.parse` leave absent properties out, and `Object.has` (a native) tells spread
  whether a source property is present.
- **Output details.** Generators print as `Object [Generator] {}` and promises as `Promise { 1 }` /
  `Promise { <pending> }` / `Promise { <rejected> Error: x }`, as in Node.js; stack traces through `await` show the
  frames of the current continuation.

## References

- Static TypeScript (MPLR 2019): https://mmoskal.github.io/pdf/static-typescript.pdf
- MakeCode language notes: https://makecode.com/language
- ECMAScript 2025 Language Specification: https://tc39.es/ecma262/
- BearSSL: https://bearssl.org/
- Monocypher: https://monocypher.org/
