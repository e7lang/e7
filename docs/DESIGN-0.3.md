# E7 0.3 design decisions

Status: **implemented in 0.3**, with the changes listed in [Implementation notes](#implementation-notes) at the
end. These are the design decisions the 0.3 plan ([ROADMAP.md](ROADMAP.md), section 0.3) asked for before any
code was written. Each one states what is decided, why, what it costs, and what is left open. The bytecode
changes are specified in [BYTECODE.md](BYTECODE.md), section 6 (format 3).

Every decision keeps the rules of [SPEC.md](SPEC.md) section 0.1: types are known at compile time (the fixed rule),
the bytecode is verifiable, and behaviour is identical to JavaScript as Node.js 22 implements it.

| | Decision | VM change | Format |
|---|---|---|---|
| D1 | Coroutines: a generator or `async` activation keeps its frame in a heap object between resumptions | yes | 3 |
| D2 | `async` / `await`: ECMAScript promise jobs in the VM, the host as the event loop | yes | 3 |
| D3 | Format 3: what changes, and format 2 kept for modules that do not need it | yes | 3 |
| D4 | Regular expressions: an own backtracking engine, budget-bounded, patterns checked at compile time | yes | 3 |
| D5 | Literal types: checked at compile time, represented as their base type | no | 2 |
| D6 | Recursive interfaces and type aliases | no (JSON descriptors: `requires 0.3.0`) | 2 |
| D7 | Properties that hold `undefined`: a presence mask in objects of interface / object types | yes | 3 |

D7 is the "presence bit" of the plan's item F; it is part of format 3, so it is decided here with the others.

---

## D1. Coroutines

### Decision

A generator or `async` function call creates a **coroutine object** (`CoroObj`) that owns the function's frame while
it is not running. Suspending (`yield`, `await`) copies the frame's registers and `ip` into the coroutine and pops
the frame; resuming pushes a frame on top of the current stack and copies them back. While it runs, a coroutine's
frame is an ordinary frame on the VM stack.

### Why this works with the current VM

- Script frames already live on the VM stack, addressed by offsets (`Frame::base`), not on the C stack
  (DESIGN.md section 5.4). Moving one frame is a copy of `nregs` slots.
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
  statement at that point, so `finally` blocks run through the existing handler-table mechanism; the VM has no
  separate unwinding path. `throw(e)` raises `e` at the `YIELD` instruction, where the handler tables of the
  generator function apply as for any `throw`.
- **Re-entrancy.** Resuming a coroutine that is running raises `TypeError: Generator is already running`, as in
  JavaScript. Resuming a finished one returns `done = true` (`next`), its value (`return(v)`), or raises (`throw`).
- **No allocation in loops.** `RESUME` writes the value and the done flag into two registers; `for...of`, spread and
  destructuring over a generator therefore allocate no `{ value, done }` objects. Only an explicit `g.next()` in
  script code builds the result object.
- **Budget suspension** (`suspend_on_budget`) keeps working inside a running coroutine: its frame is on the stack
  like any other.
- **Natives.** A native never suspends halfway (unchanged). A generator resumed from a callback that a native made
  (a `sort` comparator calling `g.next()`) is fine: its frame is pushed above the callback's frame and popped again
  at the `yield`.

### Iterators

- `Generator<T, TReturn, TNext>`, `Iterator<T>`, `IterableIterator<T>` and `IteratorResult<T>` are built-in types.
  `IteratorResult<T, TReturn>` is the record `{ done: boolean; value: T | TReturn }`, accepted when `TReturn` is `T`,
  `void` or `undefined` (then `value` is `T | undefined`, a nullable type); any other `TReturn` is a compile error,
  because `value` would be a general union.
- A class with a method named `[Symbol.iterator]` returning `Iterator<T>` is iterable: `Symbol.iterator` is accepted
  as this one fixed method name, not as a value (`Symbol` values stay out). `for...of`, spread and array
  destructuring over it call the method and then `next()` through the vtable, and call `return()` when the loop
  exits early, as the spec requires.

### Costs and limits

- A coroutine holds `nregs` slots plus a header; it is charged to the memory limit like any object.
- `RESUME` / `YIELD` copy `nregs` slots each way. Functions with very many registers pay for it; acceptable for
  the target workloads (measured in the benchmarks).
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

- The C API names below are proposals, fixed when the API is written.
- The host registers `fetchKey` with `e7_register_async_native`. When the script calls it, the VM creates a pending
  promise and an integer **ticket**, and calls the host function with the arguments and the ticket. The host may
  settle the ticket at once or later, with `e7_settle(vm, ticket, &value)` or `e7_settle_error(vm, ticket, message)`.
- `e7_call` runs the script until the VM stack is empty and the microtask queue is drained. If the call's result is
  known (a plain function, or an `async` function whose promise is settled) and no ticket is open, it returns as
  today; a rejected promise is reported like an uncaught error. Otherwise it returns the new status **`E7_PENDING`**.
  The host settles one or more tickets and calls `e7_resume`, which drains the microtasks the settlements queued and
  returns `E7_OK`, an error, or `E7_PENDING` again.
- While a call is pending, no new `e7_call` can start (`E7_ERR_STATE`, as for a suspended call);
  `e7_abort_suspended` abandons it, and later settlements of its tickets return `E7_ERR_STATE`.
- All of this happens on the VM's thread, like every other call; a host that completes work on another thread
  marshals the settlement back.
- An exported `async` function returns `Promise<T>`; the host sees `T` (the export's host signature marks it).
- Unhandled rejections: a promise rejected with no handler when the queue is drained fails the call with the error,
  as Node.js (since 15) ends the process with it.

### Why not an event loop in the VM

E7 runs inside apps that already have one (Android's, iOS's, a server's). A second loop would need threads or
timers inside the VM, which the sandbox does not allow and which would make results depend on timing. With the host
as the loop, a script's behaviour depends only on the order in which the host settles tickets.

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
- `Promise.all` / `allSettled` / `race` / `any` take an array of `Promise<T>` with one `T` (tuples of different
  types would need a union result); `allSettled` gives `PromiseSettledResult<T>[]`, a record with `status`, `value?`
  and `reason?`.
- `new Promise<T>((resolve, reject) => ...)` is supported: `resolve` and `reject` are closures over the promise.

### In and out of scope

In: `async` functions, methods and arrow functions, `await` (also top-level in the entry module), `Promise<T>` with
`then` / `catch` / `finally`, `Promise.resolve` / `reject` / `all` / `allSettled` / `race` / `any`, `new Promise`,
async generators and `for await`, `AggregateError`. Out: timers (`setTimeout`, `setInterval`, `queueMicrotask` is
possible later), `Promise.withResolvers` (ES2024, after 0.3), thenables that are not promises (a class with a `then`
method is not awaited specially; tsc types it as a plain object, and so does E7).

### Bindings

Each language binding wraps `E7_PENDING` in its own model: Kotlin `suspend fun call(...)` with a coroutine continuation
per ticket, Swift `async` with `CheckedContinuation`, C# `Task<T>` with `TaskCompletionSource`, JavaScript / WebAssembly
`Promise`. Host async functions are registered with the language's async function type.

---

## D3. Bytecode format 3

### Decision

0.3 introduces format 3 ([BYTECODE.md](BYTECODE.md) section 6). The compiler writes **format 2 whenever a module uses
no format-3 feature**, so the 0.2.x VMs in the field keep running everything they can. A 0.3 VM loads formats 2
and 3.

Format 3 adds, compared with format 2:

- a function flags byte (generator, `async`), checked by the verifier: `GEN_START`, `YIELD` and `AWAIT` appear only
  in functions with the matching flag, `GEN_START` / `ASYNC_START` only as the first instruction;
- a class flags byte (records with a presence mask, D7);
- a regular expression table (source and flags, D4);
- new opcodes: `GEN_START`, `ASYNC_START`, `YIELD`, `RESUME_I/F/R`, `AWAIT`, `FIELD_ABSENT`; promise and iterator
  operations are natives (they need no new verification rules).

Format-3 features set `requires` to 0.3.0; `e7c --target 0.2` refuses them with a compile error that names the
feature.

### Why a new format and not new opcodes in format 2

Format 2's promise (BYTECODE.md section 5) is that every 0.2.x VM runs every 0.2.x module. New opcodes alone would
be handled by `requires`, but the function and class flags change the layout of the module, which a format-2 reader
cannot skip. A new format number makes the rejection clean (`E7_ERR_VERSION` before parsing the body).

### Compatibility tests

- Backward: the CI job runs the modules compiled by the 0.2.6 compiler (`tests/compat/0.2.6/`) on the 0.3 VM.
- Forward: the existing job runs the 0.3 compiler's format-2 output on v0.2.6.
- Rejection: a format-3 module on v0.2.6 fails with `E7_ERR_VERSION`.

---

## D4. Regular expressions

### Decision

An own regular expression engine in the VM (`src/regexp/`): a compiler from pattern to a small program and a
**backtracking matcher with an explicit stack** (no C recursion), with ECMAScript semantics on UTF-16 strings, which
is how E7 strings are stored.

- **Flags**: `d g i m s u y`. The `v` flag (set notation, ES2024) comes after 0.3.
- **Syntax**: everything ES2018 adds (named groups, lookbehind, `s`, Unicode property escapes `\p{...}`) and the
  Annex B syntax that V8 accepts without the `u` flag, so a pattern that works in Node.js works in E7.
- **Patterns are data, not code.** A module stores a literal's source and flags (D3's regular expression table);
  the VM compiles them when the module is loaded. A malformed pattern fails the load (it cannot happen for compiler
  output, since the compiler checks literals). So there is no regular expression program in the module to verify.
- **Compile-time checking**: the compiler parses every literal; a syntax error is a compile error with the position.
  `new RegExp(source, flags)` with a source built at run time raises a catchable `SyntaxError`, as in JavaScript.

### Bounded by the host's limits

Every matcher step is charged to the instruction budget (in batches, through `chargeWork`) and the backtracking stack
to the memory limit. A catastrophic pattern such as `/(a+)+$/` on a long input therefore ends with `E7_ERR_BUDGET` (or
suspends with `suspend_on_budget`, within D1's bound for natives) instead of hanging the host: ReDoS is limited by
the same budget as any other script work.

### Types (the fixed rule)

- `RegExp`, `RegExpMatchArray` (`string[]` with `index`, `input`, `groups`), `RegExpExecArray`.
- **Named groups are known for literals**: for `/(?<year>\d{4})/`, `m.groups?.year` is a `string | undefined`. For a
  pattern built at run time, `groups` has no known properties and only indexed groups are available (TypeScript types
  `groups` as an index signature, which E7 does not have).
- `replace(re, fn)`: the callback's parameters are fixed by the pattern: the match, one `string | undefined` per
  capture group of a literal, then the offset and the input (and the groups object for a literal with named groups).
  For a run-time pattern only `(match: string) => string` is accepted.
- `matchAll` returns an `IterableIterator<RegExpExecArray>` (D1), so regular expressions come after generators.

### Unicode data

`\p{...}` (General_Category, Script, Script_Extensions and the binary properties) and case-insensitive matching need
Unicode tables. They are generated from the Unicode version of the ICU that Node.js 22 uses, so results match V8.
The tables' size is measured; if it is large (the estimate is 30-60 KB), a build option leaves them out and `\p{...}`
then fails the load with a clear error.

### Verification

The test262 RegExp tests (the parts within the flags above), a random pattern-and-input fuzzer compared with
V8, ReDoS tests that check the budget bound, and the existing GC-stress, allocation-failure and sanitizer runs.

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

### Change for existing programs

Today string literal types are treated as `string`, so `let m: "GET" | "POST" = s` compiles with any string `s`. With
D5 it is a compile error, as in tsc. This is a source-level change listed in the 0.3 CHANGELOG; tsc already rejects
those programs, so no program that type-checks under tsc is affected.

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
- **`JSON.parse(text) as Tree`**: the JSON type descriptor (a string the VM interprets) gains a back-reference to an
  enclosing record (`@<depth>`). A module that uses it has `requires 0.3.0`; the format stays 2.

No VM change otherwise: objects of recursive types are ordinary records.

---

## D7. Properties that hold `undefined` (a presence mask)

### Decision

Records whose type has optional properties or properties whose type includes `undefined` get a **presence mask**:
one bit per field after the fields (one 64-bit word per 64 fields), marked in the class descriptor (format 3 class
flags). A new record has every bit set; `FIELD_ABSENT` clears the bit of a property that an object literal or a
spread leaves out, and any assignment to the property sets it again. Reading a field is unchanged (an absent field reads as
`undefined`), so property reads cost nothing extra.

`Object.keys` / `entries` / `values`, spread, `Object.assign`, `JSON.stringify` and `console.log` consult the mask:
a property is listed when its bit is set, also when it holds `undefined`, as in JavaScript. Records without the class
flag (all format-2 modules) keep the 0.2 behaviour (a property holding `undefined` is missing), so 0.2 modules run
unchanged on 0.3.

### Why a mask and not an "absent" value

An "absent" sentinel stored in the field would have to be translated to `undefined` on every read of every
reference field, a cost on the hottest path. The mask costs one bit per field and a store on writes of the flagged
records only (`SETFIELD` on a class with the flag; the class is known from the object, so the check is one branch).

### Effect

Removes the `undefined` row from SPEC section 9: with `exactOptionalPropertyTypes` (0.2.6) and the mask, a record
behaves like a JavaScript object for every property its type declares.

---

## Open questions (to settle during implementation, not blocking)

1. `console.log` of a suspended generator (`Object [Generator] {}`) and of a promise (`Promise { 1 }`,
   `Promise { <pending> }`): print as Node.js does; the inspector needs the promise state.
2. Stack traces through `await` (Node.js prints `at async f`): E7's stack traces are its own format; follow Node's
   ordering, decide the exact text with the debugger work.
3. Whether `queueMicrotask` is worth adding with D2 (cheap, but rarely used by the target scripts).
4. The size budget for the Unicode tables (D4), measured on the iOS and Android libraries.

---

## Implementation notes

What 0.3 implements differs from the decisions above in these points:

- **D1.** The value kinds in fflags are 0 none, 1 `I`, 2 `F`, 3 `R` (a plain function has fflags 0), and bits 6-7
  hold the kind of the body's return value. `GEN_START` is the first instruction, so default parameter values of a
  generator are evaluated when the body first runs (at the first `next()`), not at the call. Iterators are
  generators at run time: an iterator object written by hand (an object with a `next()` method) is not an
  `Iterator<T>`; a class is iterable through a generator method `*[Symbol.iterator]()`. `next()` on a finished
  generator whose return type is a primitive gives NaN / 0 / false instead of `undefined` (the value's
  representation is fixed).
- **D2.** The C API is `e7_register_async_native`, `e7_settle`, `e7_settle_error`, `e7_open_tickets` and the status
  `E7_PENDING` (15). A call returns `E7_PENDING` whenever host tickets are open at its end, also when the entry
  function is not `async` (the call is finished when the work it started is). An uncaught error of the call itself
  runs the jobs it queued first (as JavaScript does), then ends the call. For tests, `e7:test` provides `later(v)`
  and `laterReject(e)`: promises the VM settles itself, one per turn, in creation order (`setImmediate` in the
  Node.js shim). **Async generators (`async function*`) came in 0.3.1** (they were left out of 0.3.0): a coroutine
  of function kind 3 that starts like a generator (`GEN_START`), whose `next` / `throw` / `return` requests are
  `RESUME_R` with an "async request" bit in its extra word (BYTECODE.md section 6.2). The VM keeps the request queue
  of the specification in the coroutine (AsyncGeneratorEnqueue, Resume, Yield, CompleteStep, DrainQueue,
  AwaitReturn): a request gets a promise at once and runs the body only when the generator waits at its start or at
  a `yield`; `YIELD` in an async generator answers the first request and, when another is queued, continues without
  suspending; `RET_*` and an error leaving the frame answer the current request and drain the queue. The compiler
  emits the awaits around them (`yield v` and `return v` await `v`, `return(v)` at a `yield` awaits `v` before
  returning, `for await` awaits each request's promise), so the number of promise jobs is the specification's and
  the order of callbacks is Node's. The IteratorResult records are built by the VM, of the record class the request
  names (the caller's view of `{ value, done }`). Two choices follow V8 where the specification differs or is
  unsettled: `yield*` returns the inner iterator's value after `return(v)` without awaiting it again, and `yield*`
  of a sync iterable is not supported (its async-from-sync iterator's `throw()` changed in recent editions and
  differs between Node.js versions), so it is a compile error.
- **D3.** As decided. Records get the presence class flag only when the module may need format 3: `e7c --target 0.2`
  compiles them without masks (the 0.2 behaviour).
- **D4.** As decided; the engine is `src/regexp/` (a parser following V8's, a compiled program, a matcher with two
  explicit stacks), its Unicode tables are generated from Node.js by `scripts/gen-regexp-unicode.mjs` (about 59 KB
  compiled). The `v` flag and duplicate named groups are rejected, as Node.js 22 does.
- **D5.** As decided, with `as const` for literals and arrays and objects of literals; a `const` initialized with a
  literal has the literal's type, a `let` the base type (TypeScript's widening). An enum member used as a type
  (`kind: E.A`) is the enum's type (tsc checks the member).
- **D6.** The back-reference in a JSON descriptor is `@<n>` (the n-th enclosing object descriptor); a module using
  one calls the natives `JSON.parseRec` / `JSON.tryParseRec` (`since 0.3.0`), which is what sets its `requires`.
- **D7.** Records whose type has a property that may hold `undefined` (optional or `| undefined`) carry the mask;
  object literals, spread and `JSON.parse` leave absent properties out, `Object.has` (a native) tells spread
  whether a source property is present.

Open questions: generators print as `Object [Generator] {}` and promises as `Promise { 1 }` / `Promise { <pending> }`
/ `Promise { <rejected> Error: x }`, as in Node.js; stack traces through `await` show the frames of the current
continuation; `queueMicrotask` is not added.

