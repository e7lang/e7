# E7 Language Specification (v1.0)

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
  labeled blocks. **Not supported**: `for...in`, `with`, `try/catch/finally`.
- Function declarations are hoisted; nested functions and arrow functions are closures.
- `throw new Error(msg)` / `throw "msg"`: **terminates the script** (E7 has no exception catching); the host gets `E7_ERR_RUNTIME` and a call stack with line numbers.

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
  `public/private/protected/readonly` modifiers are accepted but not enforced at runtime.
  Arrow function fields can capture `this`. **Not supported**: getters/setters, abstract classes, `#private`, generic classes, class expressions, methods as values (use arrow functions).
- Uninitialized fields start as `0` / `false` / `null`; optional fields start as `undefined`.

## 6. Standard library

The following behave the same as JavaScript (verified by differential testing against Node.js):

- **Globals**: `parseInt parseFloat isNaN isFinite encodeURIComponent decodeURIComponent encodeURI decodeURI`,
  `Number(x) String(x) Boolean(x) BigInt(x)`, `NaN Infinity`, `console.log/info/warn/error`, `print`, `abort(msg)`.
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
`JSON.parse: expected a number at $.items[0].price`. Extra fields are ignored.

## 7. Modules

- `import { a, b as c } from "./x"`, `import * as ns from "./x"`, `import type {...}`, `export function/const/class/interface/type/enum`,
  `export { a, b as c }`. Paths are relative to the current file and the extension may be omitted (tries `.ts`, `.e7`, `/index.ts`). Circular dependencies are forbidden.
- Built-in modules: `e7:crypto`, `e7:encoding` (API in `lib/e7.d.ts`).
- Host functions: `declare function name(a: string, n: number): string;`; parameters/return values are limited to
  `number boolean string`, fixed-width integers, `Uint8Array KeyHandle void`, and are checked against the signature registered by the host when the module is loaded.
- `export function`s in the entry file can be called by the host (only parameters and return values of the types above can cross the boundary). Top-level code runs once at load time,
  and module-level variables persist across calls.

## 8. Runtime errors

The following terminate the current call (the host receives an error code and a `file:line` call stack); the VM itself remains usable:
array/string/byte out-of-bounds access, null reference access, failed `!`/`as` assertions, integer division by zero, `throw`/`abort`, JSON validation failure,
failed strict `Map` lookups, instruction budget exhausted (`E7_ERR_BUDGET`, or suspend with `E7_SUSPENDED`), memory limit (`E7_ERR_MEMORY`),
call stack too deep (`E7_ERR_STACK`), and host function errors.

## 9. Known differences from JavaScript (summary)

| Scenario | JavaScript | E7 |
|---|---|---|
| Out-of-bounds array/string read | `undefined` | Runtime error |
| Elements of `new Array<T>(n)` | Holes (read as `undefined`) | `0` / `false` / `null` (`undefined` for reference types) |
| Fixed-width integer arithmetic | Floating point | Machine integers that wrap around |
| `console.log` of long arrays/nested objects | Multi-line, columned format | Single-line format (`JSON.stringify` is identical) |
| `JSON.parse` | Returns `any` | Requires `as T`, validated against the type |
| Case conversion / `localeCompare` | Full Unicode / locale | Latin/Greek/Cyrillic / code-unit comparison |
| Inserting after many deletions during Map iteration | Order defined by the spec | May skip new elements due to table compaction (very rare) |
| Exceptions | `try/catch` | Not supported; `throw` terminates the script |
