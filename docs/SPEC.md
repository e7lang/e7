# E7 Language Specification

E7 is a **strict subset** of TypeScript: every program accepted by the E7 compiler is also accepted by `tsc --strict` (with `lib/e7.d.ts`),
and produces the same result when run on a JavaScript engine (except for fixed-width integers, see §4.2). The converse does not hold: E7 rejects TS
constructs whose types cannot be determined statically, or that cannot be compiled efficiently/safely.
[TYPESCRIPT.md](TYPESCRIPT.md) lists TypeScript feature by feature with its status in E7, the reason for each
restriction and the E7 alternative; every row is a sample program checked against the compiler (tests/ts-features).

Source file extension is `.ts` (recommended, supported directly by IDEs) or `.e7`; the compiled output is `.e7b`.

### 0.1 What defines E7

There is no official standard for a subset of TypeScript (TypeScript itself has had no maintained specification
since 1.8; `--strict` is a set of type-checking options, not a subset of the language). E7 is therefore defined by
three references that can each be checked mechanically:

1. **Upper bound: TypeScript 5.6 `tsc`** with the configuration in
   [`tests/tsc/tsconfig.json`](../tests/tsc/tsconfig.json): `strict` (which includes `strictNullChecks`,
   `noImplicitAny`, `useUnknownInCatchVariables`, ...) and `exactOptionalPropertyTypes`, with `lib/e7.d.ts`
   declaring the built-in modules and integer types. Every E7 program must type-check there; CI checks every test
   and example. The TypeScript version changes only in a minor E7 release, after the test suite passes with the new
   version.
2. **Run-time semantics: ECMAScript (ECMA-262)**, as implemented by Node.js 22 (V8). An E7 program prints what the
   same program prints on Node.js; CI compares every test byte for byte, and a differential fuzzer compares random
   programs. The differences that remain are listed in section 9: E7 extensions (fixed-width integers), E7
   checks that end the call where JavaScript would continue, and a few output details that later releases remove.
3. **The subset itself: this specification**, built from three rules. A TypeScript feature is in E7 when all three
   hold:
   - **Types are known at compile time — the fixed rule.** Every value has a static type that fixes its
     representation; nothing is decided by inspecting values at run time. This rule is never relaxed: a feature
     that needs it relaxed stays out of E7, however common it is in TypeScript. It excludes `any`, `unknown` as a
     declared type, unions other than with `null`/`undefined`, index signatures, `keyof`/conditional/mapped types,
     `eval`, and dynamic property access.
   - **The bytecode is verifiable.** The compiled module can be checked completely by the bytecode verifier before
     it runs, so a malicious module cannot corrupt memory (docs/BYTECODE.md section 3).
   - **Behaviour equals JavaScript.** If E7 cannot reproduce JavaScript's behaviour exactly, the construct is a
     compile error rather than something that runs differently (section 9 lists the exceptions that remain).

   Features that satisfy the rules but are not implemented yet (auto-accessors, ...) are listed as such. Regular
   expressions satisfy them too but are left out to keep the runtime small (section 6.6).
   [TYPESCRIPT.md](TYPESCRIPT.md) lists every TypeScript feature with its status, the reason and what to write instead; each row is a sample program that CI compiles (and, for supported features, compares with
   Node.js), so the table always matches the compiler.

---

## 1. Lexical structure

- Comments `//`, `/* */`; automatic semicolon insertion (ASI) is the same as JS (including the restriction on a newline after `return`).
- Identifiers are ASCII only (`[A-Za-z_$][A-Za-z0-9_$]*`); string contents may be any Unicode.
- Numbers: decimal, `0x`/`0o`/`0b`, exponents, numeric separators `1_000`; **bigint** literals `123n`. Legacy octal `017` is not allowed.
- Strings: `'...'`, `"..."`, escapes `\n \t \r \b \f \v \0 \xHH \uHHHH \u{H...}`; template strings `` `a ${expr} b` `` (nestable).
- Private names `#x` for class members (section 5).
- **Not supported**: decorators `@`; regular expression literals `/pattern/flags` (section 6.6). A `/` where an
  expression can start is read as one and reported as a compile error; elsewhere (after a value, an identifier, `)`
  or `]`) it is a division.

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
| `enum` | Numeric or string enum (compile-time constants: literals or constant expressions of other members, `+ - * / % ** \| & ^ << >> >>> ~` and string `+`) | number / string |
| `(a: A) => R` | Function type (closure) | Closure object |
| `T \| null`, `T \| undefined`, `T \| null \| undefined`, optional `x?: T` | Nullable types | Reference; primitive types are boxed automatically |
| `Map<K, V>`, `Set<T>` | Insertion-ordered, keys compared with SameValueZero | Heap object |
| `Uint8Array`, `ArrayBuffer`, `DataView` | Byte buffers and views (`subarray` shares memory) | Heap object |
| `TextEncoder`, `TextDecoder` | UTF-8 encoding/decoding | — |
| `KeyHandle` (from `e7:crypto`) | Reference to a key held by the host | Handle |
| `void`, `never`, `null`, `undefined` | | |

Also: `Promise<T>`, `Generator<T, TReturn, TNext>` / `Iterator<T>` / `IterableIterator<T>` (coroutines at run
time; `IteratorResult<T>` is `{ value: T | undefined; done: boolean }`, or `{ value: T; ... }` when the return type is
`T`), the typed arrays `Int8Array` ... `Float64Array`, `Uint8ClampedArray`, `BigInt64Array`, `BigUint64Array`, and
literal types: string, numeric and boolean literals and unions of literals of one base type (`"GET" | "POST"`,
`0 | 1`, `true`), checked as tsc checks them (narrowed by `===`, `!==` and `switch`) and represented as their base
type. The utility types `Awaited<T>`, `NonNullable<T>`, `NoInfer<T>` and `Uppercase` / `Lowercase` / `Capitalize`
/ `Uncapitalize` (of literal types) are resolved at compile time.

**Not supported** (compile error with a hint): `any`, `unknown`, `object`, union types other than with `null/undefined` (and
unions of literals of one base type), intersection types, index signatures (use `Map<K, V>`), `keyof`/`typeof` type
operators, conditional/mapped/template literal types, utility types such as `Record/Partial/Pick`, `Symbol`.
User-defined generics are supported (section 2.3).

### 2.1 Assignment compatibility
- Identical types; `null`/`undefined` → the corresponding nullable type; `T` → `T | null | undefined`.
- Subclass → superclass; class/object type → interface (**structural**: every property of the target exists in the source with the same type, and every method signature is the same).
- Arrays of reference elements are covariant (`Circle[]` → `Shape[]`, consistent with TS).
- Optional properties and fields follow tsc's `exactOptionalPropertyTypes`: `x?: T` may be left out but not set to
  `undefined` (by an object literal, an assignment or a spread of a property that may hold `undefined`); declare
  `x?: T | undefined` to allow it. An optional method `m?(): T` is an optional property of function type: call it as
  `o.m?.()`, or as `o.m()` after a check (`if (o.m)`).
- Numeric enum → `number`, string enum → `string`.
- Function types: parameter and return types must match; an arrow function used as a callback may **declare fewer parameters** (extra arguments are ignored), and a `void`-returning context accepts any return value.
- There is **no implicit conversion** between fixed-width integers and `number`, or between integers of different widths; use explicit conversions such as `u32(x)` or `Number(x)`.

### 2.2 Type inference and narrowing
- Variables, return values, arrow function parameters (contextual typing), and object/array literals can all be inferred; an empty array `[]` needs an annotation or a contextual type.
  As in tsc, an array literal destructured by an array pattern is a tuple when its elements have different types
  (`const [n, s] = [1, "x"]` binds a `number` and a `string`; nested patterns and literals likewise); with elements of
  compatible types it is an array. `x as const` gives literal types, readonly tuples and objects with readonly
  properties (for literals and arrays and objects of literals). A `const` initialized with a literal has the
  literal's type (`const k = "a"` is a `"a"`); a `let` has its base type, as in tsc.
- A recursive function that omits its return type is an error; an explicit annotation is required.
- Interfaces and object type aliases may be recursive (`interface Tree { children: Tree[] }`, also mutually recursive
  and generic ones); structural compatibility between recursive types is checked coinductively, as in tsc.
  `JSON.parse(text) as T` into a recursive type works (its descriptor refers back to the enclosing type).
- A `switch` with a case for every member of an enum or every value of a literal type is exhaustive: a function may
  end with it without a `default` or a final `return`, and its `default` branch sees the value as `never`
  (`assertNever(x)` patterns compile). Each case sees the value narrowed to its literals.
- Type predicates `(x: T): x is U` and `this is U`, and assertion functions `asserts x is U` / `asserts x`, narrow
  at their calls (nullable values, literal types, classes); the function's check is trusted, as in TypeScript.
- Narrowing: `x === null`, `x !== undefined`, `x == null`, truthiness checks `if (x)`, `instanceof`, combinations with `&&`/`||`/`!`,
  early `return/throw/break/continue`, `x ??= v`, and assignment (also an assignment that ends an `if` branch, when the
  other path is narrowed to the same type: `if (x === null) x = [];`, and a conditional expression whose branches are
  both non-null: `x = x === null ? [] : x`). Property paths, including static fields (`C.#instance`), are supported (after `this.head !== null`, `this.head.next` can be used directly).
  Variables reassigned inside a loop are re-checked at the loop condition; inside closures, narrowing is kept only for `const` variables (same as TS).

### 2.3 Generics

Generic functions (`function first<T>(xs: T[]): T`), generic arrow functions assigned to a `const`
(`const pair = <A, B>(a: A, b: B): [A, B] => [a, b]`), generic classes (`class Box<T>`) with generic methods
(`map<U>(f: (x: T) => U): Box<U>`), generic interfaces and type aliases (`interface Pair<A, B>`,
`type Result<T> = { ok: boolean; value: T | null }`) are compiled by **monomorphization**: each distinct list of
type arguments gets its own copy of the function, class or type, checked and compiled with the concrete types
(no runtime type information, no VM support needed).

- **Type arguments** are written explicitly (`first<string>(xs)`, `new Box<number>(1)`, `Pair<number, string>`) or
  inferred like `tsc` does in the common cases: from the arguments (function literal arguments are typed from what
  the other arguments determine, then contribute their return type), from the expected type of the result
  (`const b: Box<string[]> = new Box([])`), and from default type parameters (`<T = number>`). A type parameter
  that cannot be inferred is a compile error (TypeScript would infer `unknown`). Inference unifies `T` with
  `null`/`undefined` into a nullable type; conflicting candidates are reported as argument type errors.
- **Constraints** (`<T extends Shape>`, `<T extends Comparable<T>>`) are checked for every instantiation; inside the
  generic code a type parameter *is* its concrete type, so the body only needs to type-check for the arguments
  actually used (it must still pass `tsc`). `keyof` constraints are not supported.
- **Generic classes**: `Box<number>` and `Box<string>` are separate classes (named `Box` at run time, so
  `console.log` prints `Box { ... }`); a generic class can extend and be extended by generic and plain classes
  and implement generic interfaces. Static members belong to the class itself, not to an instantiation (one
  copy shared by all instances, as in TypeScript; they cannot use the type parameters). Instances with different
  type arguments are not assignable to each other (`Box<Circle>` is not a `Box<Shape>`; TypeScript compares them
  structurally), and `x instanceof Box` is not supported for a generic class.
- **Generic methods** (instance and static) are called directly, not through the class: they cannot override or
  be overridden, and cannot be abstract. Generic methods in interfaces, object types and object literals, and
  generic function types (`<T>(x: T) => T`) are not supported.
- A generic function used as a value needs a contextual function type that determines its type arguments
  (`xs.map(identity)`); a generic arrow function must be assigned to a `const` (each instantiation is its own
  closure, created where the `const` is declared).
- Stack traces and debug information name instances with their type arguments (`first<string>`,
  `Box<number>.map<string>`), and compile errors in generic code name the instantiation: `... (in first<string>)`.
- The number of instantiations is bounded (500 per generic declaration, 4000 per program), and so is their
  nesting: a generic that instantiates itself with ever larger type arguments (`f<T>(x: T) { f<T[]>([x]) }`,
  polymorphic recursion) is a compile error.

## 3. Declarations and statements

- `let` / `const` (**`var` is not supported**); block scoping; `let` in a loop gets a fresh binding per iteration (closure capture behaves as in JS).
- Array/object/tuple destructuring (declarations, assignment `[a, b] = [b, a]`, function parameters, `for...of`), nested.
  In declarations, parameters and `for...of`, elements can have **default values** (used when the value is
  `undefined`, or past the end of an array; evaluated only then, and they may refer to earlier bindings) and a
  **rest element**: `const [first, ...others] = xs` (a new array; for a tuple, a tuple of the remaining
  elements), `const { id, ...rest } = obj` (a new object with the other properties of the static type, without
  `#private` fields). A binding with a default has the element type without `undefined`. Assignments to existing
  variables support array and object patterns with defaults, nesting and rest (`[a, b] = [b, a]`,
  `({ host, port = 80, ...rest } = options);`); a pattern with defaults, nesting or rest, and every object pattern,
  must be a statement of its own. A rest element must be a variable or property, not a pattern.
- Object spread `{ ...a, k: v, ...b }`: the source may be an object type or a class instance (only fields are copied, not methods); later properties with the same name override earlier ones;
  **optional** properties in the source override only when present (same as TS: in `{ retries: 3, ...opts }`, `retries` has type `number`).
  The properties of the result are in JavaScript's order (entries in source order, a key keeps its first position): the
  first spread source whose type is an object type contributes its properties in their run-time order (the value is
  tested against the classes and object layouts it can have), other spread sources in the order of their type. A possibly missing property that is later assigned by an
  explicit property is placed at the end (consistent with JS when that property is missing).
- `if / else`, `while`, `do...while`, `for(;;)`, `for...of` (arrays, strings by code point, `Uint8Array`, `Map`, `Set`,
  `arr.entries()/keys()/values()`, `map.keys()/values()/entries()`), `switch` (with fall-through), `break/continue` (including labels),
  labeled blocks, `try`/`catch`/`finally` (section 3.1). **Not supported**: `for...in`, `with`.
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
- `try { } finally { }` and `try { } catch { } finally { }`: the finally block runs when the try block (and the
  catch block) completes normally, by `return`, `break` or `continue`, and when a catchable error leaves it (the
  error is rethrown after the finally block), as in JavaScript: a `return` value is computed before the finally
  block runs; a `return`, `throw`, `break` or `continue` in the finally block replaces the pending completion
  (and discards a pending error); nested finally blocks run innermost first.
- Errors that cannot be caught (below) end the call **without running finally blocks**, like they skip catch
  blocks. An error that passes through a finally block is rethrown from it: its stack trace starts there, and a
  debugger that stops on caught errors also stops when the finally block takes it.
- The compiler copies a finally block for every way out of its try statement, so deeply nested finally blocks
  can make a function too large (a compile error).
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
- No fast-math optimizations: results are bit-identical across Android/iOS/Windows/Linux/macOS/WebAssembly, on
  every architecture (no fused multiply-add contraction; SSE2, not x87, on 32-bit x86).
- `Math.acos acosh asin asinh atan atanh atan2 cbrt cos cosh exp expm1 log log1p log2 log10 sin sinh tan tanh pow`
  and `**` use V8's fdlibm code (`third_party/fdlibm`), never the platform C library: every result is the same
  double as in Node.js 22 on x64 (verified bit for bit on millions of arguments per function, docs/PRODUCTION.md). `Math.hypot`
  is V8's algorithm; `sqrt fround round trunc floor ceil abs sign clz32 imul max min` are exact. Node.js itself is
  not uniform here: its arm64 builds contract fdlibm into fused multiply-adds (about 0.1% of `sin`, `log`, ...
  results differ in the last bit), and Node.js 23+ computes `Math.pow` / `**` with the C library's pow unless run
  with `--no-use-std-math-pow` (`10 ** -5` is `0.000009999999999999999` in e7 and Node.js 22, `0.00001` there).

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
- Comparing types with no overlap (such as `string === number`) is a compile error (same as TS). Any value can be compared
  with `null` / `undefined` (also a non-nullable one, or a variable narrowed to a non-nullable type, as tsc accepts); the
  result of such a comparison is known at compile time. A `void` value cannot be compared.
- `&&` and `||` can be used with any type in conditions (by truthiness); in value position, `a || b` returns a value (types must be compatible), and `&&` is boolean only.
- `||=` and `&&=` assign when the target is falsy / truthy (after `x ||= v` with a non-null `v`, `x` is non-null).
  The comma operator and `void` work as in JavaScript (a variable holding `void x` needs a type annotation, as for
  `undefined`).
- `??`, `??=`, `?.` (members, element access `a?.[i]`, method calls, function calls) are the same as JS. An
  optional chain short-circuits as a whole: in `a?.b.c`, `a?.b[i]` or `a?.m().n()` the links after `?.` are
  skipped (the result is `undefined`) when `a` is null or undefined, and type-checked against the non-null
  type; a link whose own type is nullable (`a?.b.c` with an optional `b`) still needs its own `?.`, as in tsc.

### 4.5 Other
- `typeof x` is evaluated at compile time to a constant string (not supported for nullable types).
- `x as T`: same type, upcast, removing nullability (runtime-checked), class downcast (runtime-checked, terminates on failure), `JSON.parse(text) as T` (§6.4).
- `x!` non-null assertion: runtime-checked, terminates on null/undefined.
- Tagged templates `` tag`a${x}b` `` call `tag(["a", "b"], x)` (the parts as a `readonly string[]`, TypeScript's
  `TemplateStringsArray`; its `raw` property is not available); `` String.raw`...` `` gives the raw text.
- `<T>x` is the same as `x as T`.
- Not supported: `delete`, `in`, `new.target`, `eval`.

## 5. Functions and classes

- Parameters must have type annotations (arrow functions can infer them from context). Default parameters (which may reference earlier parameters), optional parameters `x?: T`, and rest parameters `...xs: T[]` are supported.
  A function declaration with default parameters, and a built-in function (`Math.sqrt`, `parseInt`, `String`,
  `Boolean`, ...), becomes a value where a function type is expected (`xs.map(Math.sqrt)`, `["1", "2"].map(parseInt)`
  passes the index as the radix, as in JavaScript): the compiler wraps it in an arrow function.
  Arrow functions and function expressions can have default parameters too: such a closure parameter has the
  type `T | undefined` (a default applies when the argument is omitted or `undefined`, as in JavaScript).
  A callback parameter annotated `unknown` takes its contextual type (`Array.from({ length: n }, (_: unknown, i) => i)`).
- Spread arguments: a tuple supplies one argument per element (`f(...pair)`, evaluated once); arrays, tuples and
  other iterables can be spread into a rest parameter of a user function anywhere in the argument list (the rest
  parameter is always a new array); built-in functions such as `Math.max` take arrays only. Spreading an array into
  other parameters is a compile error (as in TypeScript). A default value may refer to earlier parameters.
- A local function value may call itself (`const f = (n: number): number => ... f(n - 1)`) when its type is known
  without its initializer (typed parameters and a return type, or an annotation); so may a named function
  expression through its name (`function f(n: number): number { ... f(n - 1) ... }`).
- Classes: fields (optionally initialized, types can be inferred), constructor parameter properties (`constructor(public x: number)`), methods, static fields/methods,
  single inheritance with `extends` (the first statement of a derived class constructor must be `super(...)`; if the constructor is omitted, the superclass constructor parameters are inherited automatically),
  `super.method()`, `implements` (checked structurally), `instanceof`. A method override may have fewer parameters
  and a return type assignable to the overridden method's (a subclass of its class), with the same representations
  (the overridden method's other parameters are passed and ignored). A method returning `this` gives the receiver's
  class at a call. `declare x: T` declares a field set elsewhere (also an inherited field, with its type).
  `public/private/protected` modifiers are accepted but not enforced. `readonly` is enforced at compile time as in TypeScript: a readonly
  field (or parameter property) can only be assigned through `this` in its own class's constructor, and a `static readonly` field only by its
  initializer. A `readonly` property of an interface or object type cannot be assigned; `readonly T[]` /
  `ReadonlyArray<T>`, `readonly [A, B]`, `ReadonlyMap` and `ReadonlySet` have no mutating methods and no element or
  `length` writes, and are not assignable to their mutable types (the other way round is fine). Mutable static fields can be written with `=`, compound assignment and `++`/`--`. Static fields, methods and accessors
  are **inherited** as in JavaScript: `B.x` reads the static member of the nearest base class that has one; writing an inherited
  static field through a subclass (`B.x = v`, compound assignment, `++`) gives that subclass its own copy (the base class keeps
  its value), which the subclass and its own subclasses read from then on. `#private` static members are not inherited (as in
  JavaScript, `B.#x` is an error). Writing an array's `length` truncates or
  extends it (`a.length = n`); compound assignment and `++`/`--` on `length` are not supported, and `length` of strings and `Uint8Array`
  is read-only. Members of an imported namespace (`import * as ns`) cannot be assigned.
  Arrow function fields can capture `this`.
- **Accessors** `get x()` / `set x(v)` (instance and static) are compiled to method calls: they are virtual
  (overridable), `super.x` calls the base class accessor, and compound assignment and `++` call the getter and
  the setter once each. A getter and its setter have the same type; a setter without a parameter type takes the
  getter's. Accessors are not own properties (not in `Object.keys`, `JSON.stringify`, spread). Interfaces and
  object types may declare accessors (`get size(): number; set size(v: number)`), implemented by class accessors;
  interface *properties* are read as fields, so a class cannot implement one with an accessor (compile error).
  Destructuring a class value calls its getters. Object literals can have accessors too (below).
- **Abstract classes** cannot be instantiated; abstract methods and accessors need a return type and are
  overridden by every concrete subclass. An abstract property (`abstract kind: string`) is declared as a field
  (of the same type) by the concrete subclasses and read or written by name through the abstract class type.
- **`#private`** fields, methods and accessors (`get #x()` / `set #x(v)`; instance and static) can only be used inside the class body that declares
  them (including static blocks and closures there). Private methods are not virtual. Private fields are not
  own enumerable properties: `Object.keys`, `JSON.stringify`, `console.log`, object spread and rest skip them.
  Private methods and accessors are not virtual. `#x in obj` and redeclaring a base class's private name in a subclass are not supported.
- **Static blocks** `static { ... }` run with the static field initializers, in source order, when the class
  declaration is executed; they can use the class's private static members.
- **Not supported**: class expressions, decorators, methods as values (use arrow functions). Generic classes: section 2.3.
- Object literals can have **methods** (`{ n: 2, twice(x: number): number { return x * this.n; } }`): `this` is
  the object (or the contextual interface type), and the methods are own properties holding functions, as in
  JavaScript. The literal's type has methods, so it can be passed as an interface with methods; such an
  interface can also be implemented by arrow function properties. Methods cannot be used as values, and a
  literal with methods cannot contain spreads.
- Object literals can have **getters and setters** (`{ n: 2, get twice(): number { return this.n * 2; },
  set twice(v: number) { this.n = v / 2; } }`). `this` is the object, as in methods; the
  accessors can use the object's fields, other accessors and methods, and capture variables. They are part of
  the literal's type, as accessors (`get twice(): number; set twice(v: number)`), never as fields, so every
  read or write of the property calls the getter or the setter (by name, like an interface's accessors; the
  representation is in [BYTECODE.md](BYTECODE.md) section 6.4):
  - the literal is assignable to an interface (or an object type) that declares the accessors (`get x(): T` /
    `set x(v: T)`), and is then used like a class implementing them; an interface that declares `x` as a
    *property* reads it as a field, so assigning the literal to it is a compile error that says to declare
    `get x(): T` there (the rule for classes, section 5 above). A getter without a setter is read-only (writing
    it is a compile error, as in tsc); a setter without a getter cannot be read. A getter and its setter have
    one type (a setter without a parameter type takes the getter's, a getter without a return type the setter's).
  - The type of a literal with accessors holds only such literals (whose accessors are own properties): a
    class instance or a value of a declared interface type is not assignable to it.
  - The accessors are own enumerable properties, in the literal's order: `Object.keys` lists them;
    `JSON.stringify`, spread, rest, `Object.values` / `entries` / `assign` (as a source) and destructuring call
    the getters; `console.log` prints `x: [Getter]`, `[Setter]` or `[Getter/Setter]` without calling them, as
    Node.js does. Through an interface type that declares accessors, `Object.keys`, `JSON.stringify`,
    `console.log` and `Object.values` / `entries` see the run-time object (a literal's accessors are listed, a
    class's are not); spread, rest and `Object.assign` copy the properties of the static type (section 9).
  - Compile errors: a property and an accessor with the same name, two getters (or setters) of one name, a
    literal with accessors and spreads, a plain property implementing an interface's accessor, `structuredClone`
    of a type with accessors (the copy would have data properties), and spread / rest / `Object.values` /
    `entries` / `assign` of a literal with a setter-only accessor (its value would be `undefined`).
- Uninitialized fields start as `0` / `false` / `null`; optional fields start as `undefined`.

### 5.1 Generators and iterators

- `function*` declarations and expressions, generator methods (`*m()`, also `*[Symbol.iterator]()`), `yield`,
  `yield*` (of a generator, an array or an iterable class: values, `next(v)` arguments, `return(v)` and `throw(e)`
  are forwarded), `next(v)` / `return(v)` / `throw(e)` (`throw` takes an `Error`).
- Types: `Generator<T, TReturn = void, TNext = (none)>`, `Iterator<T>` and `IterableIterator<T>` (a generator is all
  three at run time). `TReturn` must be `void` or `T` (otherwise `IteratorResult` would be a general union); a return
  type left out is inferred (`Generator<yielded types, returned types>`).
- A generator, or an instance of a class with a `*[Symbol.iterator]()` method, works with `for...of`, spread,
  `Array.from` and array destructuring. Leaving a `for...of` early (`break`, `return`, a `throw` from the body) calls
  the iterator's `return()`, so its `finally` blocks run, as in JavaScript.
- `next()` on a finished generator whose return type is a primitive gives `NaN` / `0` / `false` for `value`
  (section 9); default parameter values of a generator are evaluated by the first `next()`.

### 5.2 `async` functions and promises

- `async` functions, methods and arrow functions, `await` (also top-level in the entry module; an imported module
  with top-level `await` is a compile error), `for await (const x of xs)` over arrays (each element awaited) and
  generators.
- Async generators: `async function*` declarations and expressions, async generator methods
  (`async *m()`, also `async *[Symbol.asyncIterator]()` and in object literals). Types:
  `AsyncGenerator<T, TReturn = void, TNext = (none)>`, `AsyncIterator<T>`, `AsyncIterableIterator<T>` and
  `AsyncIterable<T>` (all async generators at run time; `TReturn` is `void` or `T`, as for generators). In the body,
  `yield v` awaits `v` (a `Promise<T>` yields its value) and `return v` awaits `v`; `yield*` takes an async generator
  or an instance of a class with an `[Symbol.asyncIterator]()` method returning one (values, `next(v)` arguments,
  `return(v)` and `throw(e)` are forwarded), or a sync iterable: an array, a generator or an iterable class, through
  JavaScript's async-from-sync iterator (values that are promises are awaited, a rejected one closes the iterator;
  `throw(e)` into an array rejects with a `TypeError`, `return(v)` gives `v`; a generator of promises must return
  `void`). These two closing rules are ECMAScript 2025, as in Node.js 24; Node.js 22 implements the earlier rules
  (`throw(e)` rejects with `e`, a rejected promise does not close the sync iterator). `next(v)` / `return(v)` / `throw(e)` return
  `Promise<IteratorResult<T, TReturn>>`; requests made while the generator runs or awaits are queued and answered in
  order, and `return` / `throw` on a generator that has not started or has finished behave as in JavaScript (the
  request queue of the specification). `for await (const x of it)` over an async iterable awaits each `next()`;
  leaving the loop early (`break`, `return`, a `throw` from the body) awaits `it.return()`, so the generator's
  `finally` blocks run. A generator prints as `Object [AsyncGenerator] {}`.
- `Promise<T>`: `then` (callbacks returning `U` or `Promise<U>`), `catch` (a handler returning `T`, or nothing),
  `finally`, `new Promise<T>((resolve, reject) => ...)` (`reject` takes an `Error`; an error thrown by the executor
  rejects), `Promise.resolve` / `reject` / `all` / `allSettled` / `any` / `race` over arrays of promises of one type
  (`allSettled` gives records `{ status, value?, reason? }`, `any` rejects with an `AggregateError`).
- The VM runs the promise jobs of the ECMAScript specification in its order, so callbacks run in the order of
  Node.js. A call runs its frames, then the jobs they queued, until none is left. An `async` function returns its
  promise at its first `await`; an exported `async` function gives the host the promise's value. A rejection that
  no handler takes by the end of the call fails the call with its error (Node.js ends the process).
- There are no timers. Host operations are `declare function f(...): Promise<T>` functions the host registers with
  `e7_register_async_native`; their promises are settled by the host through tickets, and a call returns
  `E7_PENDING` while tickets are open (see EMBEDDING.md). For tests, `later(v)` / `laterReject(e)` of `e7:test`
  settle in a later turn (section 7.1).

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
  and `String.fromCharCode/fromCodePoint`. `split`, `replace` and `replaceAll` take a string pattern (there are no
  regular expressions, section 6.6, so `match matchAll search` are compile errors); the replacement of `replace` /
  `replaceAll` can be a function `(match, offset, string) => string`; case conversion covers Latin, Greek and Cyrillic letters;
  `localeCompare` compares by code units (locale-independent).
- **Array**: `length push pop shift unshift at slice splice concat indexOf lastIndexOf includes join reverse fill sort
  map filter forEach reduce reduceRight some every find findIndex findLast findLastIndex flatMap flat`
  (`flat(depth)` with a constant depth, an integer literal or `Infinity`), `Array.from` (an iterable or
  `{ length: n }, fn`), `Array.of`, `new Array<T>(n)`, writable `length`. The default `sort()` sorts as strings (same as JS); sorting is stable.
  **Out-of-bounds reads are runtime errors** (JS returns `undefined`); writing at index `length` appends.
- **Map / Set**: `size get set has delete clear forEach keys values entries`, iteration, spread, copy construction.
- **Uint8Array / ArrayBuffer / DataView**: `length byteLength byteOffset buffer set subarray slice fill indexOf lastIndexOf
  includes join reverse at forEach`, `Uint8Array.from/of`, all DataView `get/set{Uint8..Float64,BigInt64,BigUint64}` (big/little endian).
- **TextEncoder / TextDecoder** (UTF-8; decoding replaces invalid sequences per WHATWG rules).
- **bigint**: all arithmetic/bitwise/comparison operations, `BigInt.asUintN/asIntN`, `toString(radix)`, use as Map keys.
- **JSON.stringify** (including the indent argument; tuples are output as arrays; `undefined` fields are omitted; circular references are an error).
- **Object**: `keys(obj)`; `values(obj)` / `entries(obj)` for object and class types whose properties all have
  one type (an optional property counts with its type without `undefined` and is skipped when absent, as with
  `keys`): the properties come in their run-time order, and a property that the static
  type does not declare (an object with more properties than its type) is an E7 error; `assign(target, ...sources)`:
  with an object literal target it is the literal `{ ...target, ...sources }`, otherwise the target's type must have
  every source property (copied like object spread, optional ones only when present); `freeze(obj)`.
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

### 6.6 Regular expressions (not supported)

E7 has no regular expressions ([DESIGN.md](DESIGN.md) D4: the engine and its Unicode tables would make every app's
runtime about 160 KB larger). A regular expression literal, the types `RegExp`, `RegExpMatchArray` and
`RegExpExecArray`, `new RegExp(...)` and the String methods `match`, `matchAll` and `search` are compile errors
(`regular expressions are not supported in E7; use string methods ...`). `split`, `replace` and `replaceAll` take
string patterns. A host that needs pattern matching can offer it as a host function.

## 7. Modules

- Imports: `import { a, b as c } from "./x"`; `import x from "./x"` (the default export; also
  `import x, { a } from`, `import x, * as ns from` and `import { default as x } from`); `import * as ns from "./x"`;
  `import type ...` and inline `type` modifiers; `import "./x"`, which binds nothing and only makes x run. Imported
  bindings are live (an imported `let` shows the exporting module's assignments) and cannot be assigned. Names
  imported with `import type` (or an inline `type`) are types only; `import type` and `export type ... from` are
  erased as in TypeScript, so a module reached only through them is type-checked but never run.
- Namespace imports: through `ns`, every export of the module is available as in TypeScript: functions and
  constants (`ns.f()`, `ns.PI`), classes (`new ns.C()`, `ns.C.staticMember`, `x instanceof ns.C`,
  `class D extends ns.C`), enums (`ns.E.A`) and types in type positions (`ns.T`, `ns.I`, `ns.E`, `ns.Box<number>`),
  and namespaces that the module re-exports (`ns.inner.f()`). The members of a namespace cannot be assigned (a
  class's static fields can: `ns.C.count = 0`). A local declaration with the same name shadows the namespace.
- Exports: `export function/const/let/class/interface/type/enum`, `export { a, b as c }` (also
  `export { a as default }`), and `export default` followed by a function or class declaration, an interface, or an
  expression. An anonymous `export default function` / `class` is named `default`, as in JavaScript.
  `export default <expression>` evaluates the expression once, at its place in the module's top-level code, into a
  hidden constant; when the expression is just the name of a declaration of the module (a function, class, enum,
  type, `const` or import) that declaration is exported, and a `let` is exported with its value at that point
  (not as a live binding), as in JavaScript. A module has at most one default export, and cannot export one name for two different bindings.
- Re-exports: `export { a, b as c } from "./x"` (`default` may appear on either side of `as`),
  `export type { T } from "./x"`, `export * as ns from "./x"`, and `export * from "./x"`, which re-exports every
  export of x except its default export. Re-exports create no local bindings. A module's own exports
  (declarations, export lists and named re-exports) take precedence over `export *`; a name that two `export *`
  provide with different bindings is a compile error, as in TypeScript (JavaScript leaves such a name out of the
  module), while the same binding reached through two `export *` is not a conflict. Functions of the built-in
  modules can be re-exported too (`export { sha256 } from "e7:crypto"`, `export * from "e7:encoding"`).
- Initialization order: each module's top-level code runs once, after the modules it depends on (its imports,
  side-effect imports and re-exports, in source order, depth first), so the entry file runs last; this is
  JavaScript's evaluation order.
- Paths are relative to the current file and the extension may be omitted (tries `.ts`, `.e7`, `/index.ts`). Circular dependencies are forbidden.
- Built-in modules: `e7:crypto`, `e7:encoding`, `e7:json`, `e7:test` (API in `lib/e7.d.ts`; `e7:json` is described in section 6.5, `e7:test` in section 7.1).
- Host functions: `declare function name(a: string, n: number): string;`; parameters/return values are limited to
  `number boolean string`, fixed-width integers, `Uint8Array KeyHandle void`, and are checked against the signature registered by the host when the module is loaded.
- The functions that the entry file exports (declared there or re-exported; a default export under the name `default`) can be called by the host (only parameters and return values of the types above can cross the boundary). Top-level code runs once at load time,
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

Also `later<T>(value: T): Promise<T>` and `laterReject<T>(error: Error): Promise<T>`: a promise settled in a
later turn, each one after the previous (as `setImmediate` in Node.js, which the shim of the differential tests
uses), for tests of asynchronous ordering. They do not fail.

`assertEqual` / `assertNotEqual` accept any one type for both operands (the checker treats them as a builtin
generic `<T>(actual: T, expected: T)`). The second operand is checked
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
| `console.log` of a few other values | `DataView {...}`, `[Map Iterator] {...}`, `{ encoding: 'utf-8' }` | A `DataView` prints as a `Uint8Array`, `map.keys()` (and the other iterators) as an array, `TextEncoder`/`TextDecoder` as `null` |
| `yield*` of a sync iterable in an async generator | Node.js 22: `throw(e)` into an array rejects with `e`; a sync generator that yields a rejected promise is not closed | ECMAScript 2025 (Node.js 24): a `TypeError`, and the generator is closed (its `finally` runs) |
| Key order of optional properties added later | A property added to an object goes last | The property order of an object is its layout, fixed when it is created: an object literal puts the optional properties of its type that it does not set last, in declaration order, so properties added later appear in JavaScript's order unless several are added in another order |
| `next()` on a finished generator whose return type is a primitive (`Generator<number, number>`, also `AsyncGenerator`) | `{ value: undefined, done: true }` | `value` is `NaN` (number), `0` / `false` (integers, booleans): the value's representation is fixed |
| Default parameter values of a generator function (also `async function*`) | Evaluated at the call | Evaluated when the body first runs (the first `next()`) |
| Iterators written by hand (an object with `next()`) | Any object with `next()` | Iterators are generators (`function*`, `async function*`); a class is iterable through `*[Symbol.iterator]()` / `async *[Symbol.asyncIterator]()` |
| Asynchronous host operations, timers | The event loop (`setTimeout`, I/O) | No timers; host functions returning promises settle through tickets, and a call returns `E7_PENDING` while tickets are open (the host is the event loop) |
| `JSON.parse` | Returns `any` | Requires `as T`, validated against the type |
| JSON of the wrong shape | Accepted (no validation) | `JSON.parse(text) as T` ends the call (not catchable); `tryParse(text) as T \| null` from `e7:json` returns `null`. Syntax errors are a catchable `SyntaxError` in both |
| `Array.join` / `console.log("%s")` of class instances | Calls the class's `toString()` | `"[object Object]"` (`String(x)`, template literals and `+` do call `toString()`) |
| Case conversion / `localeCompare` | Full Unicode / locale | Latin/Greek/Cyrillic / code-unit comparison |
| Exceptions | Every error is catchable | `try`/`catch` catches the errors Node.js also throws; E7-specific checks and resource limits end the call (section 3.1) |
| E7's own runtime checks (out of bounds, failed `as`, limits) | — | Not catchable; `finally` blocks do not run |
| `Object.values` / `entries` of an object with more properties than its type | All own properties | E7 error (the extra property's type is unknown) |
| `Object.assign` with an object having more properties than its type | Copies them all | Copies the properties of the static type (like spread) |
| Spread, rest and `Object.assign` of an object literal with accessors typed as an interface that declares them | The getters' values are copied (own properties) | The properties of the static type are copied: an interface's accessors are not (a class instance's are not own properties); spread the literal's own type to copy them |
| `structuredClone` of an object literal with accessors behind a type that does not show them | A copy with data properties | An E7 error (a value of a type with accessors is a compile error) |
| Instances of a generic class with different type arguments | Structurally compatible | Different classes (`Box<Circle>` is not a `Box<Shape>`) |
| `console.log` of an instantiated generic function value | `[Function: f]` | `[Function: f<number>]` |
