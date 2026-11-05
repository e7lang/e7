# E7 language reference (for writing code)

E7 accepts a statically typed subset of TypeScript and runs it with JavaScript semantics. This page lists
what you can use, what the compiler rejects, and how to write each rejected construct in E7.
The normative specification is `docs/SPEC.md` in github.com/e7lang/e7.

Code blocks marked `ts` here are complete programs that compile; blocks marked `ts e7-reject` show
TypeScript that E7 rejects (both kinds are checked against the real compiler in E7's CI).

Contents

1. Types
2. Rejected TypeScript features and their E7 rewrites
3. Statements and expressions
4. Functions and classes
5. Modules and host functions
6. Runtime differences from JavaScript

## 1. Types

| Use | Notes |
|---|---|
| `number`, `boolean`, `string`, `bigint` | `number` is an IEEE double exactly as in JS |
| `i8 u8 i16 u16 i32 u32 i64 u64`, `int` (= `i64`) | machine integers: wrap on overflow, `/` truncates, division by 0 is a runtime error |
| `T[]`, `Array<T>`, tuples `[A, B]` | typed element storage |
| `interface`, `type` aliases of object types, object literals | structural typing, like TS |
| `class` | nominal; single inheritance |
| `enum` (numeric or string) | compile-time constants |
| `(a: A) => R` | closures |
| `T \| null`, `T \| undefined`, optional `x?: T` | the only allowed unions |
| `Map<K, V>`, `Set<T>` | insertion-ordered, SameValueZero keys |
| `Uint8Array`, `ArrayBuffer`, `DataView`, `TextEncoder`, `TextDecoder` | bytes |
| `Date` | UTC only |
| `KeyHandle` (from `e7:crypto`) | a key held by the host app |

Integers never convert implicitly. Integer literals adapt to the expected type (`const x: u8 = 200`) and are
range-checked (`const y: u8 = 256` is an error).

```ts
const a: u32 = 0xffffffff;
const b: u32 = a + 1;           // wraps to 0
const n: number = Number(a) + 1; // 4294967296
const c = u32(n);               // explicit conversion, wraps to 0
console.log(b, n, c, rotl32(u32(1), 31), u8(300));
```

## 2. Rejected TypeScript features and their E7 rewrites

### `any`, `unknown`, `object`

```ts e7-reject
function show(x: any): string { return String(x); }
```

Use the concrete type, an interface, or one function per type:

```ts
interface Point { x: number; y: number }
function showPoint(p: Point): string { return `(${p.x}, ${p.y})`; }
console.log(showPoint({ x: 1, y: 2 }));
```

### Union types other than with `null`/`undefined`

```ts e7-reject
function size(x: string | number): number { return typeof x === "string" ? x.length : x; }
```

```ts e7-reject
type Shape = { kind: "circle"; r: number } | { kind: "rect"; w: number; h: number };
```

Use one interface with a discriminating field (string or enum) and optional fields, or separate functions:

```ts
enum ShapeKind { Circle, Rect }
interface Shape { kind: ShapeKind; r?: number; w?: number; h?: number }

function area(s: Shape): number {
  switch (s.kind) {
    case ShapeKind.Circle: return Math.PI * (s.r ?? 0) ** 2;
    case ShapeKind.Rect: return (s.w ?? 0) * (s.h ?? 0);
  }
  return 0;
}
console.log(area({ kind: ShapeKind.Rect, w: 2, h: 3 }));
```

### Exceptions: `try` / `catch` / `finally`

```ts e7-reject
function parse(text: string): number {
  try { return JSON.parse(text) as number; } catch (e) { return 0; }
}
```

`throw` ends the call (the host gets the error and a stack trace); nothing in the script can catch it.
Validate before acting, and return failures as values:

```ts
function parsePort(text: string): number | null {
  const n = Number(text);
  return Number.isInteger(n) && n > 0 && n < 65536 ? n : null;
}
const port = parsePort("8080");
console.log(port === null ? "invalid" : `port ${port}`);
```

In tests, `assertThrows` from `e7:test` can check that a call fails.

### User-defined generics

```ts e7-reject
function first<T>(items: T[]): T { return items[0]; }
```

```ts e7-reject
class Box<T> { constructor(public value: T) {} }
```

Built-in generic types (`Array<T>`, `Map<K, V>`, `Set<T>`) and generic built-in methods (`map`, `filter`,
`reduce`, ...) work. For your own code, write concrete types:

```ts
function firstOrNull(items: string[]): string | null { return items.length > 0 ? items[0] : null; }
class NumberBox { constructor(public value: number) {} }
console.log(firstOrNull(["a", "b"]), new NumberBox(3).value);
```

### Index signatures, `Record`, and utility types

```ts e7-reject
const prices: { [sku: string]: number } = { pen: 2 };
```

```ts e7-reject
const totals: Record<string, number> = {};
```

```ts e7-reject
interface User { name: string; age: number }
function patch(u: User, p: Partial<User>): User { return { ...u, ...p }; }
```

Use `Map` for dynamic keys and spell out interfaces (optional fields for partial updates):

```ts
const prices = new Map<string, number>([["pen", 2], ["book", 12]]);
prices.set("bag", 30);
let total = 0;
for (const [sku, price] of prices) total += price;
console.log(prices.get("pen") ?? 0, total, prices.has("cup"));

interface User { name: string; age: number }
interface UserPatch { name?: string; age?: number }
function patch(u: User, p: UserPatch): User { return { ...u, ...p }; }
console.log(JSON.stringify(patch({ name: "Ann", age: 30 }, { age: 31 })));
```

`JSON.parse(text) as Map<string, V>` reads a JSON object with arbitrary keys.

### Regular expressions

```ts e7-reject
const digitsOnly = /^[0-9]+$/.test("123");
```

Use string methods (`split`, `replace`/`replaceAll` with string patterns, `indexOf`, `startsWith`,
`endsWith`, `trim`, `charCodeAt`):

```ts
function isDigits(s: string): boolean {
  if (s.length === 0) return false;
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c < 48 || c > 57) return false;
  }
  return true;
}
console.log(isDigits("123"), isDigits("12a"), "a-b-c".replaceAll("-", "+"), "k=v".split("="));
```

### `async`, `await`, `Promise`, timers, I/O

```ts e7-reject
async function load(): Promise<string> { return "x"; }
```

Scripts are synchronous. The host app does networking and storage and passes data in (as arguments, or
through host functions declared with `declare function`). Long computations can be time-sliced by the host
with an instruction budget.

### `for...in`, `in`, `delete`, `var`, `==`

```ts e7-reject
const o = { a: 1 };
for (const k in o) console.log(k);
```

```ts e7-reject
var count = 0;
```

```ts e7-reject
const same = "1" == "1";
```

```ts
const o = { a: 1, b: 2 };
for (const k of Object.keys(o)) console.log(k);
let count = 0;
count++;
const maybe: string | null = null;
console.log(count, "1" === "1", maybe == null);   // `== null` / `!= null` are allowed
```

### Getters, setters, `#private`, abstract classes, class expressions, decorators

```ts e7-reject
class Temp { private c = 0; get f(): number { return this.c * 9 / 5 + 32; } }
```

```ts
class Temp {
  private celsius = 0;
  fahrenheit(): number { return this.celsius * 9 / 5 + 32; }
  setCelsius(c: number): void { this.celsius = c; }
}
const t = new Temp();
t.setCelsius(100);
console.log(t.fahrenheit());
```

`public` / `private` / `protected` are accepted (not enforced). `readonly` is enforced like TypeScript.

### Methods and default-parameter functions as values

```ts e7-reject
class Fmt { prefix = "#"; tag(s: string): string { return this.prefix + s; } }
const f = new Fmt();
const tags = ["a", "b"].map(f.tag);
```

```ts
class Fmt { prefix = "#"; tag(s: string): string { return this.prefix + s; } }
const f = new Fmt();
console.log(["a", "b"].map(s => f.tag(s)).join(" "));
```

### `JSON.parse` without a type

```ts e7-reject
const data = JSON.parse('{"a":1}');
```

```ts
interface Order { id: string; amount: number; items: { sku: string; qty: i32 }[]; note?: string }
const order = JSON.parse('{"id":"A1","amount":12.5,"items":[{"sku":"X","qty":2}]}') as Order;
console.log(order.id, order.items[0].qty, order.note ?? "(none)");
```

The text is validated against the type while parsing; a mismatch stops the call with the path
(`JSON.parse: expected a number at $.amount`). Extra fields are ignored.

### Destructuring with defaults or rest

```ts e7-reject
const [first, ...rest] = [1, 2, 3];
```

Plain destructuring works (`const [a, b] = pair`, `const { x, y } = point`, `for (const [k, v] of map)`,
`[a, b] = [b, a]`); use `slice` for the rest and `??` for defaults:

```ts
const nums = [1, 2, 3];
const [first, second] = nums;
const rest = nums.slice(1);
console.log(first, second, rest.join(","));
```

### Other rejected syntax

`with`, the comma operator, `void`, `delete`, `in`, `new.target`, tagged templates, `eval`, `Symbol`,
`keyof`/`typeof` in type positions, conditional and mapped types, intersection types, and typed arrays other than
`Uint8Array` (use `number[]`, `u32[]` or `DataView`). Identifiers are ASCII only.

## 3. Statements and expressions

- `let`/`const` with block scoping; a `let` in a loop gets a fresh binding per iteration.
- `if`, `switch` (fall-through allowed), `for`, `for...of` (arrays, strings, `Uint8Array`, `Map`, `Set`,
  `entries()/keys()/values()`), `while`, `do...while`, labeled `break`/`continue`.
- Object spread `{ ...a, k: v }` (objects and class instances; only fields are copied).
- `?.`, `??`, `??=`, `!` (checked at run time), `as T` (upcasts; class downcasts are checked at run time).
- Narrowing works on `=== null`, `!== undefined`, `== null`, truthiness, `instanceof`, early returns, and
  property paths (`if (this.head !== null) this.head.next ...`).
- Comparing values of unrelated types (`"1" === 1`) is a compile error, as in TypeScript.
- `typeof x` is evaluated at compile time; do not use it for runtime type dispatch.

## 4. Functions and classes

- Parameters need type annotations (arrow functions passed as callbacks infer them). Default, optional and rest
  parameters work. A recursive function needs an explicit return type.
- An empty array literal needs a type: `const xs: string[] = []`.
- Classes: fields (typed or initialized), constructor parameter properties, methods, static members,
  `extends` (derived constructors call `super(...)` first), `super.method()`, `implements` (structural),
  `instanceof`. Overrides must keep the same signature. Fields without an initializer start as
  `0` / `false` / `null`, optional fields as `undefined`.
- Arrow-function fields can capture `this` and are the way to pass "methods" around.

## 5. Modules and host functions

- `import { a, b as c } from "./x"` (extension optional), `import * as ns from "./x"`, `import type`,
  `export function/const/class/interface/type/enum`, `export { a, b as c }`. No circular imports.
- Built-in modules: `e7:crypto`, `e7:encoding`, `e7:test` (see stdlib.md and testing.md).
- Host functions: `declare function name(a: string, n: number): string;` with host types only
  (`number boolean string`, `i8`..`u64`, `Uint8Array`, `KeyHandle`, `void` result).
- `export function`s of the entry file are callable by the host; their parameters and results must be host
  types. Use a JSON string to pass structured data.
- Top-level code runs once at load; module-level variables persist between calls into the same loaded module.

## 6. Runtime differences from JavaScript

| Situation | JavaScript | E7 |
|---|---|---|
| `arr[i]`, `s[i]`, `bytes[i]` out of bounds | `undefined` | runtime error |
| `new Array<T>(n)` elements | holes | `0` / `false` / `null` |
| integer types | floating point | machine integers (wrap) |
| `JSON.parse` | `any` | must be `as T`, validated |
| `throw` | catchable | ends the call |
| `Date` local time | host time zone | UTC |
| `toUpperCase` / `localeCompare` | full Unicode / locale | Latin, Greek, Cyrillic / code units |
| `console.log` of nested values | multi-line | single line |

Runtime errors (out of bounds, `null` access, failed `!`/`as`, integer division by zero, `throw`,
`abort(msg)`, failed `JSON.parse` validation, exhausted instruction budget or memory) stop the current call; the
host receives the message and a stack trace such as `at total (cart.ts:12)`. The VM stays usable for the next call.
