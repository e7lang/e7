# E7 Language Specification (0.2)

E7 is a **strict subset** of TypeScript: every program accepted by the E7 compiler is also accepted by `tsc --strict` (with `lib/e7.d.ts`),
and produces the same result when run on a JavaScript engine (except for fixed-width integers, see §4.2). The converse does not hold: E7 rejects TS
constructs whose types cannot be determined statically, or that cannot be compiled efficiently/safely.

Source file extension is `.ts` (recommended, supported directly by IDEs) or `.e7`; the compiled output is `.e7b`.

---

## 1. Lexical structure

- Comments `//`, `/* */`; automatic semicolon insertion (ASI) is the same as JS (including the restriction on a newline after `return`).
- Identifiers are ASCII only (`[A-Za-z_$][A-Za-z0-9_$]*`); string contents may be any Unicode.
- Numbers: decimal, `0x`/`0o`/`0b`, exponents, numeric separators `1_000`; **bigint** literals `123n`. Legacy octal `017` is not allowed.
- Strings: `'...'`, `"..."`, escapes `\n \t \r \b \f \v \0 \xHH \uHHHH \u{H...}`; template strings `` `a ${expr} b` `` (nestable).
- **Not supported**: regex literals, private names `#x`, decorators `@`.

## 2. Types

| Type | Description | Runtime representation |
|---|---|---|
| `number` | IEEE-754 double, same semantics as JS | f64 register |
| `boolean` | | int register |
| `string` | Immutable, UTF-16 semantics (`length`/indexing by UTF-16 code units) | Heap object |
| `bigint` | Arbitrary-precision integer | Heap object |
| `i8 u8 i16 u16 i32 u32 i64 u64`, `int`=`i64` | **E7 extension**: machine integers (§4.2) | int64 register (normalized to bit width) |
| `T[]` / `Array<T>` | Dynamic array, typed element storage | Heap object |
| `[A, B, ...]` | Tuple (fixed length, each element independently typed) | Fixed-length record |
| `{ a: T; b?: U }`, `interface`, `type` alias | Structural object type (fields accessed by name, with inline caches) | Record/class instance |
| `class C` | Nominal type, field layout fixed at compile time, virtual method dispatch | Instance |
| `enum` | Numeric or string enum (compile-time constants) | number / string |
| `(a: A) => R` | Function type (closure) | Closure object |
| `T \| null`, `T \| undefined`, `T \| null \| undefined`, optional `x?: T` | Nullable types | Reference; primitive types are boxed automatically |
| `Map<K, V>`, `Set<T>` | Insertion-ordered, keys compared with SameValueZero | Heap object |
| `Uint8Array`, `ArrayBuffer`, `DataView` | Byte buffers and views (`subarray` shares memory) | Heap object |
| `TextEncoder`, `TextDecoder` | UTF-8 encoding/decoding | — |
| `KeyHandle` (from `e7:crypto`) | Reference to a key held by the host | Handle |
| `void`, `never`, `null`, `undefined` | | |

**Not supported** (compile error with a hint): `any`, `unknown`, `object`, union types other than with `null/undefined`, intersection types,
user-defined generics (functions/classes/interfaces/aliases), index signatures (use `Map<K, V>`), `keyof`/`typeof` type operators, conditional/mapped types,
utility types such as `Record/Partial/Pick`, `Promise`, `Symbol`, `RegExp`, and typed arrays other than `Uint8Array`, such as `Int32Array`
(use `u32[]` or `DataView`). String literal types (`"a" | "b"`) are treated as `string`.

### 2.1 Assignment compatibility
- Identical types; `null`/`undefined` → the corresponding nullable type; `T` → `T | null | undefined`.
- Subclass → superclass; class/object type → interface (**structural**: every property of the target exists in the source with the same type, and every method signature is the same).
- Arrays of reference elements are covariant (`Circle[]` → `Shape[]`, consistent with TS).
- Numeric enum → `number`, string enum → `string`.
- Function types: parameter and return types must match; an arrow function used as a callback may **declare fewer parameters** (extra arguments are ignored), and a `void`-returning context accepts any return value.
- There is **no implicit conversion** between fixed-width integers and `number`, or between integers of different widths; use explicit conversions such as `u32(x)` or `Number(x)`.

### 2.2 Type inference and narrowing
- Variables, return values, arrow function parameters (contextual typing), and object/array literals can all be inferred; an empty array `[]` needs an annotation or a contextual type.
- A recursive function that omits its return type is an error; an explicit annotation is required.
- Narrowing: `x === null`, `x !== undefined`, `x == null`, truthiness checks `if (x)`, `instanceof`, combinations with `&&`/`||`/`!`,
  early `return/throw/break/continue`, `x ??= v`, and assignment. Property paths are supported (after `this.head !== null`, `this.head.next` can be used directly).
  Variables reassigned inside a loop are re-checked at the loop condition; inside closures, narrowing is kept only for `const` variables (same as TS).

## 3. Declarations and statements

- `let` / `const` (**`var` is not supported**); block scoping; `let` in a loop gets a fresh binding per iteration (closure capture behaves as in JS).
- Array/object/tuple destructuring (declarations, assignment `[a, b] = [b, a]`, function parameters, `for...of`); default values and `...rest` in destructuring are not supported.
- Object spread `{ ...a, k: v, ...b }`: the source may be an object type or a class instance (only fields are copied, not methods); later properties with the same name override earlier ones;
  **optional** properties in the source override only when present (same as TS: in `{ retries: 3, ...opts }`, `retries` has type `number`).
  The property order of the result is fixed at compile time: a possibly missing property that is later assigned by an explicit property is placed at the end (consistent with JS when that property is missing).
- `if / else`, `while`, `do...while`, `for(;;)`, `for...of` (arrays, strings by code point, `Uint8Array`, `Map`, `Set`,
  `arr.entries()/keys()/values()`, `map.keys()/values()/entries()`), `switch` (with fall-through), `break/continue` (including labels),
  labeled blocks, `try`/`catch` (section 3.1). **Not supported**: `for...in`, `with`, `finally`.
- Function declarations are hoisted; nested functions and arrow functions are closures.
- `debugger;` stops execution when a debugger is attached ([DEBUGGING.md](DEBUGGING.md)) and does nothing otherwise (as in JS).
- `throw` takes an Error: `throw new Error(msg)` (also `TypeError`, `RangeError`, `SyntaxError`, `URIError`), an
  `Error` value, or a caught value (rethrow). Throwing strings or other values is a compile error. An error that no
  `catch` handles ends the call; the host gets `E7_ERR_RUNTIME` with the message and a call stack.

### 3.1 `try` / `catch`

```ts
try {
  const order = JSON.parse(body) as Order;
  process(order);
} catch (e) {
  if (e instanceof SyntaxError) return "malformed JSON";
  if (e instanceof Error) return "failed: " + e.message;
}
```

- `catch (e)` and `catch` without a binding. The catch variable has type `unknown` (as with TypeScript `--strict`);
  the annotation `catch (e: unknown)` is allowed. Narrow it with `e instanceof Error` (or `TypeError`, `RangeError`,
  `SyntaxError`, `URIError`) to read `e.message` and `e.name`; `String(e)` and `` `${e}` `` give `"Name: message"`
  (just `"Name"` when the message is empty). Compare caught values with `===`, rethrow them with `throw e`.
  `console.log(e)` is rejected (Node.js prints a stack trace there); `e.stack` is not supported.
- `finally` is not supported yet; run the cleanup after the `try` statement.
- **What can be caught**: errors that Node.js also throws as exceptions, with the same error name:
  - every `throw` in the script;
  - errors raised by the standard library whose JavaScript counterpart throws: `JSON.parse` syntax errors
    (`SyntaxError`), `RangeError`s (BigInt division by zero, `toFixed` / `toPrecision` / `toString(radix)` arguments,
    invalid array or string lengths, `String.repeat` counts, invalid dates, typed-array offsets, ...), `TypeError`s
    (`reduce` of an empty array without an initial value, `JSON.stringify` of cyclic structures or BigInts), `URIError`s
    (`decodeURIComponent` and friends);
  - errors from host functions whose message starts with one of the error names above (`"RangeError: ..."`).

  The message text of built-in errors is E7's own and can differ from Node.js; the name (and `instanceof`) matches.
- **What cannot be caught** (it still ends the call): checks that are specific to E7 because JavaScript would not
  throw at that point — out-of-bounds reads, failed `as` casts and `JSON.parse(...) as T` shape mismatches (use
  `tryParse`), integer division by zero, null access through a type the checker considered non-null, functions that
  end without returning a value, values read from the holes of `new Array<Error>(n)` (`undefined`; `instanceof` is
  false for them, but reading `.message` or throwing them ends the call), and e7's own size limits, whose messages say
  "an e7 limit" (JSON and array-to-string nesting deeper than 256 levels, BigInts over 262144 bits, arrays over
  2^27 - 1 and typed arrays over 2^31 - 1 elements) — and `abort()`, the instruction budget, the memory limit and the
  call depth limit.
  This keeps the rule "an E7 program prints the same output as on Node.js" true for programs that catch errors.
- A handler runs in the frame of its `try`; frames above it are dropped, including script callbacks called from
  library functions such as `Array.sort`. Assignments made in the `try` block before the error are kept.
  Catching costs instruction budget like the calls it undoes (one tick per dropped frame), and a library function
  that fails is charged for the work it did before the error.
- Errors convert like in JavaScript outside `catch` too: `JSON.stringify(e)` is `{}`, `String([e])` and template
  literals give `"Name: message"`. Printing a value that contains an Error with `console.log` is a compile error
  (Node.js prints a stack trace).
- Debuggers stop on errors that no handler catches; stopping on caught errors too is an option
  (`e7_debug_break_on_caught_errors`, the *Caught errors* exception breakpoint in VS Code).

## 4. Expression semantics

### 4.1 `number` (identical to JS)
- Integer literals are `number` by default. `/` is floating-point division, `%` is as in JS (sign follows the dividend, including `-0`).
- `& | ^ ~ << >> >>>` first apply ToInt32/ToUint32 (as in JS); the result is `number`.
- `NaN`, `Infinity`, `-0` behavior and formatting (`String(x)`, `toFixed`, `toPrecision`, `toExponential`, `toString(radix)`) are the same as JS.
- No fast-math optimizations: results are bit-identical across Android/iOS/Windows.

### 4.2 Fixed-width integers (E7 extension)
- Values annotated with types such as `u32` are true machine integers: `+ - *` wrap around; `/` is **truncating integer division**, `%` is integer remainder; division by 0 is a runtime error.
- Bitwise operations work at the type's bit width; for unsigned types `>>` is a logical shift, for signed types `>>` is an arithmetic shift and `>>>` a logical shift;
  shift counts are taken modulo 32 for types of ≤32 bits and modulo 64 for 64-bit types.
- Integer literals adapt to the contextual type and are range-checked (`const x: u8 = 256` is a compile error).
- Conversion functions `u32(x)`, `i64(x)`, etc.: from `number`, truncate the fraction and then reduce modulo 2^n (consistent with `x >>> 0`, `x | 0`); from other integer types/`bigint`, reduce modulo 2^n.
- Built-ins: `rotl32 rotr32 rotl64 rotr64 clz32 clz64 ctz32 ctz64 popcnt bswap16 bswap32 bswap64 mulhi64 imulhi64`.
- On a JS engine (where `lib/e7.d.ts` declares them as `number`) they do not wrap around; **code written as `u32(a + b)` gives the same result on both**.

### 4.3 Strings
- `+` and template strings: values of any type are converted with JS `String(x)` rules and then concatenated.
- Comparisons `< <= > >=` are lexicographic by UTF-16 code units; `===` compares contents.
- Out-of-bounds `s[i]` is a runtime error (JS returns `undefined`).

### 4.4 Equality and logic
- Only `===` / `!==` are allowed; `== null` / `!= null` are allowed as a special case meaning "is null or undefined".
- Comparing types with no overlap (such as `string === number`) is a compile error (same as TS).
- `&&` and `||` can be used with any type in conditions (by truthiness); in value position, `a || b` returns a value (types must be compatible), and `&&` is boolean only.
- `??`, `??=`, `?.` (members, method calls, function calls) are the same as JS.

### 4.5 Other
- `typeof x` is evaluated at compile time to a constant string (not supported for nullable types).
- `x as T`: same type, upcast, removing nullability (runtime-checked), class downcast (runtime-checked, terminates on failure), `JSON.parse(text) as T` (§6.4).
- `x!` non-null assertion: runtime-checked, terminates on null/undefined.
- Not supported: the comma operator, `void`, `delete`, `in`, `new.target`, tagged templates, `eval`.

## 5. Functions and classes

- Parameters must have type annotations (arrow functions can infer them from context). Default parameters (which may reference earlier parameters), optional parameters `x?: T`, and rest parameters `...xs: T[]` are supported.
  A function with default parameters cannot be passed as a value (wrap it in an arrow function).
- Classes: fields (optionally initialized, types can be inferred), constructor parameter properties (`constructor(public x: number)`), methods, static fields/methods,
  single inheritance with `extends` (the first statement of a derived class constructor must be `super(...)`; if the constructor is omitted, the superclass constructor parameters are inherited automatically),
  `super.method()`, `implements` (checked structurally), `instanceof`. Method overrides require identical signatures.
  `public/private/protected` modifiers are accepted but not enforced. `readonly` is enforced at compile time as in TypeScript: a readonly
  field (or parameter property) can only be assigned through `this` in its own class's constructor, and a `static readonly` field only by its
  initializer. Mutable static fields can be written with `=`, compound assignment and `++`/`--`. Writing an array's `length` truncates or
  extends it (`a.length = n`); compound assignment and `++`/`--` on `length` are not supported, and `length` of strings and `Uint8Array`
  is read-only. Members of an imported namespace (`import * as ns`) cannot be assigned.
  Arrow function fields can capture `this`. **Not supported**: getters/setters, abstract classes, `#private`, generic classes, class expressions, methods as values (use arrow functions).
- Uninitialized fields start as `0` / `false` / `null`; optional fields start as `undefined`.

## 6. Standard library

The following behave the same as JavaScript (verified by differential testing against Node.js):

- **Globals**: `parseInt parseFloat isNaN isFinite encodeURIComponent decodeURIComponent encodeURI decodeURI`,
  `Number(x) String(x) Boolean(x) BigInt(x)`, `NaN Infinity`, `console.log/info/warn/error`, `print`, `abort(msg)`.
  `console.log` formats values like Node.js `util.inspect` (depth 2, 80 columns, long arrays grouped into aligned
  columns, `... n more items` after 100 entries, `[Circular *1]`), including `%s %d %i %f %j %o %O %c` substitutions
  in a first string argument.
- **Math**: all functions and constants (`min/max/hypot` support varargs and spread), `Math.random()` (the host can set the seed for reproducibility).
- **Number**: `isInteger isSafeInteger isFinite isNaN parseFloat parseInt`, constants; instance methods `toString(radix) toFixed toPrecision toExponential`.
- **String**: `length charAt charCodeAt codePointAt at indexOf lastIndexOf includes startsWith endsWith slice substring substr
  toUpperCase toLowerCase trim trimStart trimEnd padStart padEnd repeat split replace replaceAll concat localeCompare`,
  `String.fromCharCode/fromCodePoint`. `split/replace` accept only string arguments (no regex); case conversion covers Latin, Greek and Cyrillic letters;
  `localeCompare` compares by code units (locale-independent).
- **Array**: `length push pop shift unshift at slice splice concat indexOf lastIndexOf includes join reverse fill sort
  map filter forEach reduce reduceRight some every find findIndex findLast findLastIndex`, `Array.from` (an iterable or
  `{ length: n }, fn`), `Array.of`, `new Array<T>(n)`, writable `length`. The default `sort()` sorts as strings (same as JS); sorting is stable.
  **Out-of-bounds reads are runtime errors** (JS returns `undefined`); writing at index `length` appends.
- **Map / Set**: `size get set has delete clear forEach keys values entries`, iteration, spread, copy construction.
- **Uint8Array / ArrayBuffer / DataView**: `length byteLength byteOffset buffer set subarray slice fill indexOf lastIndexOf
  includes join reverse at forEach`, `Uint8Array.from/of`, all DataView `get/set{Uint8..Float64,BigInt64,BigUint64}` (big/little endian).
- **TextEncoder / TextDecoder** (UTF-8; decoding replaces invalid sequences per WHATWG rules).
- **bigint**: all arithmetic/bitwise/comparison operations, `BigInt.asUintN/asIntN`, `toString(radix)`, use as Map keys.
- **JSON.stringify** (including the indent argument; tuples are output as arrays; `undefined` fields are omitted; circular references are an error).
- **Object.keys(obj)**.
- **Date** (**UTC semantics**: local time is UTC, `getTimezoneOffset()` is always 0, results match Node under `TZ=UTC`):
  `Date.now/parse/UTC`, `new Date()/new Date(ms)/new Date(string)/new Date(y, m, d?, h?, mi?, s?, ms?)/new Date(date)`,
  all `get*/getUTC*`, `getTime/setTime/valueOf` (other `set*` methods are not supported; compute with `Date.UTC` and then `setTime`), `toISOString/toJSON/toString/toUTCString/toDateString/toTimeString`,
  `<`/`>` comparison; `Date.parse` supports ISO 8601 and RFC 1123 formats, invalid dates are `NaN` (terminates on `toISOString`).

### 6.4 JSON.parse (type-checked)
In E7 it must be written as `JSON.parse(text) as T`, where `T` may be a primitive type, array, tuple, interface/object type (nested), nullable and optional fields,
or `Map<string, V>` (E7 extension: objects with dynamic keys). Parsing **validates against T**; a missing field or type mismatch terminates with a path, for example
`JSON.parse: expected a number at $.items[0].price`. Extra fields are ignored. Nesting deeper than 256 levels is a syntax error.
To handle untrusted input without terminating, use `tryParse` from `e7:json` (section 6.5).

### 6.5 `e7:json` (non-throwing typed parse)

```ts
import { tryParse, lastError } from "e7:json";
interface Config { host: string; port: number; tags?: string[] }
const cfg = tryParse(text) as Config | null;
if (cfg === null) console.log("bad config: " + lastError());  // e.g. "expected a number at $.port"
```

- `tryParse(text) as T | null` parses and validates exactly like `JSON.parse(text) as T` (section 6.4, same types
  `T`), but a syntax error or a shape/type mismatch gives `null` instead of a runtime error. The `as T | null` form
  is required: a call without `as`, or an assertion whose type does not include `null` (`as T`), is a compile error.
  `T` may itself contain `undefined` (`as T | undefined | null`).
- The `| null` is the failure value, so `T` is the asserted type without `null`. A document that is just `null` is
  therefore a mismatch (`null`, with `lastError()` saying what was expected). Because `(T | null) | null` is the same
  type as `T | null`, writing a nullable `T` does not change this: a JSON `null` document and a failure both give
  `null`, and only `lastError()` tells them apart. Nested values may be nullable as usual (`(number | null)[] | null`).
- `lastError(): string` is the reason the most recent `tryParse` of this VM returned `null`: the `JSON.parse` message
  without its `JSON.parse: ` prefix, for example `expected a number at $.amount`, `missing property at $.id`, or
  `SyntaxError: unexpected end of input at position 8`. It is `""` after a successful `tryParse` (and before the
  first one); `JSON.parse` does not change it. The value is per VM and persists across host calls.
- Limits are unchanged: nesting deeper than 256 levels gives `null` (a syntax error). The instruction budget and the
  memory limit (including the temporary parse tree) still apply and are **not** turned into `null`: exhausting them
  stops the call with `E7_ERR_BUDGET` / `E7_ERR_MEMORY` as for any other code.
- On a JavaScript engine (the Node shim `tests/node_shim/e7-json.mjs`, used for differential tests) `tryParse` is
  `JSON.parse` in a `try`/`catch`: only syntax errors (and a `null` document) give `null`, the shape is not
  validated, and `lastError()` texts differ. Differential tests therefore only use shape-correct documents or syntax
  errors; shape mismatches are tested against E7 alone (`tests/cases/lib/json_try_shape.ts` with its `.out` file).

## 7. Modules

- `import { a, b as c } from "./x"`, `import * as ns from "./x"`, `import type {...}`, `export function/const/class/interface/type/enum`,
  `export { a, b as c }`. Paths are relative to the current file and the extension may be omitted (tries `.ts`, `.e7`, `/index.ts`). Circular dependencies are forbidden.
- Built-in modules: `e7:crypto`, `e7:encoding`, `e7:json`, `e7:test` (API in `lib/e7.d.ts`; `e7:json` is described in section 6.5, `e7:test` in section 7.1).
- Host functions: `declare function name(a: string, n: number): string;`; parameters/return values are limited to
  `number boolean string`, fixed-width integers, `Uint8Array KeyHandle void`, and are checked against the signature registered by the host when the module is loaded.
- `export function`s in the entry file can be called by the host (only parameters and return values of the types above can cross the boundary). Top-level code runs once at load time,
  and module-level variables persist across calls.

### 7.1 `e7:test` (assertions)

Assertion helpers used by the `e7 test` runner; they work in any program. A failed assertion is a runtime error
(section 8): it stops the call with a message and a stack trace.

| Function | Fails when |
|---|---|
| `assert(condition: boolean, message?)` | `condition` is false |
| `assertEqual(actual, expected, message?)` | the values are not structurally equal |
| `assertNotEqual(actual, expected, message?)` | the values are structurally equal |
| `assertClose(actual: number, expected: number, tolerance = 1e-9, message?)` | `|actual - expected| > tolerance` |
| `assertThrows(fn: () => void, expectedMessage?)` | calling `fn` does not raise a runtime error, or its message does not contain `expectedMessage` |
| `fail(message?): never` | always |

`assertEqual` / `assertNotEqual` accept any one type for both operands (the checker treats them as a builtin
generic `<T>(actual: T, expected: T)`; user-defined generics are not supported). The second operand is checked
against the type of the first, unless the first is a literal whose type depends on context (an integer literal,
`null`, `undefined`, `[]`). Structural equality: numbers by value with `NaN` equal to `NaN` (and `0` equal to `-0`),
integers, booleans and enum values by value, strings / bigints / `Uint8Array`s by content, arrays element-wise,
class instances (same class) and object literals field by field, `Map`s and `Set`s by entries regardless of
insertion order, `Date`s by time, functions by identity. Failure messages show both values:

```
assertEqual failed: totals
  expected: [ 1, 3 ]
  actual:   [ 1, 2 ]
    at testTotals (cart.test.ts:8:3)
```

`assertThrows` catches the runtime errors of section 8 that are raised by the script (`throw`, `abort`, failed
assertions, out-of-bounds access, failed `JSON.parse` validation, ...), so code that rejects bad input can be
tested; instruction budget, memory limit and call depth errors are not caught. In script code, `try`/`catch`
(section 3.1) catches the subset of these errors that Node.js also throws.

Test runner: `e7 test [files or directories...]` compiles each `*.test.ts` / `*.test.e7` file (default: all of them
under the current directory, skipping `node_modules`, `build*` and hidden directories) and calls every exported
function whose name starts with `test` and that takes no parameters, each in a fresh VM (module-level state is not
shared between tests) with its own instruction budget (`--budget`, default 10^9, which includes module
initialization) and memory limit (`--mem`, default 128 MiB). A test passes when it returns normally, or returns
`true` if its return type is `boolean`. Options: `--filter <text>`, `--json`, `--verbose` (show the output of passing
tests), `--seed <n>`. The exit code is 1 if any test failed (or none ran), otherwise 0. A Node.js implementation of
`e7:test` for differential testing is in `tests/node_shim/e7-test.mjs`.

## 8. Runtime errors

The following terminate the current call unless a `catch` handles them (only the catchable errors of section 3.1
can be caught); the host receives an error code and a `file:line[:column]` call stack, and the VM itself remains usable:
array/string/byte out-of-bounds access, null reference access, failed `!`/`as` assertions, integer division by zero, `throw`/`abort`, JSON validation failure (`JSON.parse`; `tryParse` returns `null` instead),
failed strict `Map` lookups, instruction budget exhausted (`E7_ERR_BUDGET`, or suspend with `E7_SUSPENDED`), memory limit (`E7_ERR_MEMORY`),
call stack too deep (`E7_ERR_STACK`), and host function errors.

The stack trace lists one frame per line, innermost first, as `at <function> (<file>:<line>:<column>)` when the
module has debug information (always when running from source with `e7`, `e7 test`, `e7 debug`; with `e7c --debug`),
otherwise as `at <function> (<file>:<line>)` (release bytecode). Columns are 1-based UTF-8 byte offsets; a call is
located at the callee's name, a property access at the property name, an index expression at its `[`. The file is the
source file relative to the entry file (also for imported modules), top-level code appears as `<init>`, and a builtin
or host function that was running (or that called back into the script, like `Array.sort`) appears as
`at <name> (native)`. Repeated cycles of recursion are collapsed into one line such as
`... 19998 more frames of f (recursion)`.

## 9. Known differences from JavaScript (summary)

| Scenario | JavaScript | E7 |
|---|---|---|
| Out-of-bounds array/string read | `undefined` | Runtime error |
| Elements of `new Array<T>(n)` | Holes (read as `undefined`) | `0` / `false` / `null` (`undefined` for reference types) |
| Fixed-width integer arithmetic | Floating point | Machine integers that wrap around |
| `console.log` of functions | `[Function: f]` (name inferred from the variable or property) | `[Function (anonymous)]` for arrow functions and function expressions; `%o` does not list their hidden properties |
| `console.log` of a few other values | `DataView {...}`, `[Map Iterator] {...}`, `{ encoding: 'utf-8' }`; `x: undefined` for a property set to `undefined` | A `DataView` prints as a `Uint8Array`, `map.keys()` (and the other iterators) as an array, `TextEncoder`/`TextDecoder` as `null`; an object-literal field holding `undefined` is not shown (the same as a missing optional property) |
| `JSON.parse` | Returns `any` | Requires `as T`, validated against the type |
| Malformed or mismatching JSON | `JSON.parse` throws, `catch` it | `tryParse(text) as T \| null` from `e7:json` returns `null` |
| Case conversion / `localeCompare` | Full Unicode / locale | Latin/Greek/Cyrillic / code-unit comparison |
| Inserting after many deletions during Map iteration | Order defined by the spec | May skip new elements due to table compaction (very rare) |
| Exceptions | `try/catch` | Not supported; `throw` terminates the script |
