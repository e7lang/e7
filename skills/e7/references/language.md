# E7 language reference (for writing code)

E7 accepts a statically typed subset of TypeScript and runs it with JavaScript semantics. This page lists
what you can use, what the compiler rejects, and how to write each rejected construct in E7.
The normative specification is `docs/SPEC.md` in github.com/e7lang/e7.
The same repository has `docs/TYPESCRIPT.md`, a feature-by-feature table of TypeScript (handbook chapters,
JavaScript syntax and standard library): supported / partial / unsupported, the reason and the E7 alternative
for each row, with a sample program per row in `tests/ts-features`. Look a feature up there when this page does
not cover it.

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

`try`/`catch` catches errors that Node.js also throws (with the same name): `throw`, `JSON.parse` syntax errors,
standard-library `RangeError` / `TypeError` / `URIError`s, and invalid input to the `e7:encoding` decoders (a
`SyntaxError`). The catch variable is `unknown`: narrow it with `instanceof Error` or write `(e as Error).message`
(only Errors can be thrown, so a caught value always is one):

```ts
function parse(text: string): number {
  try {
    return JSON.parse(text) as number;
  } catch (e) {
    if (e instanceof SyntaxError) return 0;
    throw e;
  }
}
console.log(parse("41") + 1, parse("{"));
```

```ts e7-reject
try { console.log(1); } catch (e) { console.log(e.message); } // (e as Error).message, or if (e instanceof Error)
```

```ts e7-reject
throw "failed"; // throw new Error("failed")
```

`finally` runs as in JavaScript: after the `try` (and `catch`) block completes, returns, breaks, continues or
throws a catchable error (which is rethrown afterwards); a `return` or `throw` in `finally` replaces the
pending one:

```ts
const log: string[] = [];
function withResource(fail: boolean): string {
  log.push("open");
  try {
    if (fail) throw new Error("failed");
    return "done";
  } finally {
    log.push("close");
  }
}
console.log(withResource(false));
try { withResource(true); } catch (e) { console.log(String(e)); }
console.log(log.join(","));
```

E7's own runtime checks are not catchable, because JavaScript would not throw there: an out-of-bounds read,
`JSON.parse(text) as T` with JSON of the wrong shape, integer division by zero, `abort()`, and the budget and
memory limits. They end the call without running `finally` blocks. Validate before acting, and return expected
failures as values:

```ts
function parsePort(text: string): number | null {
  const n = Number(text);
  return Number.isInteger(n) && n > 0 && n < 65536 ? n : null;
}
const port = parsePort("8080");
console.log(port === null ? "invalid" : `port ${port}`);
```

For JSON that may have the wrong shape, use `tryParse` from `e7:json`, which returns `null` for malformed or
mismatching JSON:

```ts
import { tryParse, lastError } from "e7:json";
function parseCount(text: string): number {
  const n = tryParse(text) as number | null;
  return n === null ? 0 : n;
}
console.log(parseCount("41") + 1, parseCount("oops"), lastError());
```

In tests, `assertThrows` from `e7:test` can check that a call fails.

### Generics: what stays unsupported

Generic functions, generic arrow functions assigned to a `const`, generic classes and methods, generic
interfaces and type aliases work (section 4: they are compiled once per type argument list). Rejected: generic
methods in interfaces or object literals, generic function types, `keyof` constraints, `instanceof` with a generic
class, and a generic function used as a value without a contextual function type:

```ts e7-reject
interface Mapper { map<U>(f: (x: number) => U): U[] }
```

```ts e7-reject
function get<T, K extends keyof T>(o: T, k: K): T[K] { return o[k]; }
```

```ts e7-reject
function id<T>(x: T): T { return x; }
const f = id;
```

Write a concrete function type, or wrap the call: `const f = (x: number) => id(x)`.

`Box<Circle>` and `Box<Shape>` are different classes in E7 (one class per instantiation), so one is not
assignable to the other even when TypeScript accepts it:

```ts e7-reject
class Shape { name = "s"; }
class Circle extends Shape { r = 1; }
class Box<T> { constructor(public v: T) {} }
const c = new Box(new Circle());
const d: Box<Shape> = c;
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

### No regular expressions

E7 has no regular expressions: a `/.../` literal, `RegExp`, `match`, `matchAll` and `search` are compile errors
(an engine and its Unicode tables would add about 160 KB to every app). Use the string methods.

```ts
const line = "key = value ; other";
const eq = line.indexOf("=");
console.log(line.slice(0, eq).trim(), line.slice(eq + 1).split(";")[0].trim());
console.log("2024-11-06".split("-").map((p) => Number(p)), "a-b-c".replaceAll("-", "+"));
const isDigits = (s: string) => s.length > 0 && [...s].every((c) => c >= "0" && c <= "9");
console.log(isDigits("123"), isDigits("12a"));
```

```ts e7-reject
console.log(/\d+/.test("123"));
```

### `async`, `await`, `Promise`, generators

```ts
async function double(x: number): Promise<number> {
  return x * 2;
}
function* range(n: number): Generator<number> {
  for (let i = 0; i < n; i++) yield i;
}
async function main(): Promise<void> {
  const xs = await Promise.all([...range(3)].map(double));
  console.log(xs);
  try {
    await Promise.reject(new Error("no"));
  } catch (e) {
    if (e instanceof Error) console.log("caught", e.message);
  }
}
main();
```

`async` functions, methods and arrows, `await`, top-level `await` (entry module), `for await`, `Promise`
(`then` / `catch` / `finally`, `new Promise<T>(...)`, `Promise.all` / `allSettled` / `any` / `race` over
promises of one type) and generators (`function*`, `yield`, `yield*`, `[Symbol.iterator]` generator methods)
work as in Node.js, callbacks in the same order. There are no timers and no I/O: the host does networking
and storage through async host functions (`declare function fetchKey(id: string): Promise<Uint8Array>`,
settled by the host; see EMBEDDING.md) or passes data in as arguments.

Async generators (`async function*`) produce values over time; `for await` consumes them and
calls `return()` when the loop is left early, so `finally` blocks run:

```ts
async function* pages(n: number): AsyncGenerator<string[]> {
  try {
    for (let i = 0; i < n; i++) {
      await null;  // e.g. an async host function fetching page i
      yield [`item${2 * i}`, `item${2 * i + 1}`];
    }
  } finally {
    console.log("closed");
  }
}

async function* take<T>(src: AsyncIterable<T>, n: number): AsyncGenerator<T> {
  if (n <= 0) return;
  let k = 0;
  for await (const x of src) {
    yield x;
    if (++k >= n) return;
  }
}

async function main(): Promise<void> {
  for await (const page of take(pages(10), 2)) console.log(page.join(","));
  const g = pages(1);
  console.log(await g.next(), await g.next());
}
main();
```

The types are `AsyncGenerator<T, TReturn, TNext>`, `AsyncIterator<T>`, `AsyncIterableIterator<T>` and
`AsyncIterable<T>`; `next` / `return` / `throw` return a `Promise` of `{ value, done }` and may be called without
awaiting (the requests are queued, as in JavaScript). A class is an async iterable through
`async *[Symbol.asyncIterator]()`. `yield*` takes another async generator, or an array, a generator or an
iterable class (values that are promises are awaited, as in JavaScript).

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

### Class expressions and decorators

Getters and setters, `#private` fields, methods and accessors (`get #x()`), `abstract` classes and `static {}`
blocks work (see section 4). Class expressions, decorators and `#x in obj` are rejected:

```ts e7-reject
const Point = class { x = 0; };
```

`public` / `private` / `protected` are accepted (not enforced); `#private` members are enforced (only usable
inside their class). `readonly` is enforced like TypeScript.

Interfaces read properties as fields: a class that implements an interface property with a getter is
rejected. Declare the accessor in the interface instead (`interface Sized { get size(): number }`):

```ts e7-reject
interface HasArea { readonly area: number }
class Square implements HasArea { constructor(private side: number) {} get area(): number { return this.side ** 2; } }
```

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
(`JSON.parse: expected a number at $.amount`). Extra fields are ignored. For untrusted input write
`tryParse(text) as Order | null` (from `e7:json`): same validation, but `null` instead of ending the call.

### Recursive types

Interfaces and object type aliases may refer to themselves (also mutually, and generic ones):

```ts
interface TreeNode { label: string; children: TreeNode[] }
interface List<T> { head: T; tail: List<T> | null }
function size(t: TreeNode): number { return 1 + t.children.reduce((n, c) => n + size(c), 0); }
const root: TreeNode = { label: "root", children: [{ label: "a", children: [] }] };
const list: List<number> = { head: 1, tail: { head: 2, tail: null } };
console.log(size(root), list.tail?.head);
```

`JSON.parse(text) as T` into a recursive type works too:

```ts
interface TreeNode { label: string; children: TreeNode[] }
const t = JSON.parse('{"label":"x","children":[{"label":"y","children":[]}]}') as TreeNode;
console.log(t.children[0].label);
```

### Destructuring assignments

Destructuring declarations, parameters and `for...of` support defaults, nesting and rest elements
(`const [first = 0, ...rest] = nums`, `const { host = "localhost", ...other } = options`). Assignments to existing
variables support them too when the assignment is a statement of its own (an object pattern needs parentheses):

```ts
let first = 0;
let rest: number[] = [];
[first, ...rest] = [1, 2, 3];
let host = "", port = 0;
({ host, port = 80 } = { host: "h" } as { host: string; port?: number | undefined });
console.log(first, rest.join(","), host, port);
```

An object pattern inside a larger expression is rejected:

```ts e7-reject
let a = 0;
const r = ({ a } = { a: 1 });
```

### Other rejected syntax

`with`, `delete`, `in`, `new.target`, `eval`, `Symbol` values, `keyof`/`typeof` in type positions, conditional,
mapped and template literal types, intersection types.
Identifiers are ASCII only. (The comma operator, `void`, tagged templates, `String.raw`, `<T>x` assertions and
all typed arrays are supported.)

## 3. Statements and expressions

- `let`/`const` with block scoping; a `let` in a loop gets a fresh binding per iteration.
- `if`, `switch` (fall-through allowed), `for`, `for...of` (arrays, strings, `Uint8Array`, `Map`, `Set`,
  `entries()/keys()/values()`), `while`, `do...while`, labeled `break`/`continue`.
- `try` / `catch` / `finally`, `throw new Error(...)`.
- Destructuring with defaults and rest: `const [a = 1, ...rest] = xs`, `const { x, y = 0, ...others } = p`,
  in parameters (`function f({ host = "h" }: Opts = {})`) and `for...of`.
- Object spread `{ ...a, k: v }` (objects and class instances; only fields are copied, not `#private` ones).
- Spread arguments: tuples into any parameters (`f(...pair)`), arrays and tuples into rest parameters.
- Object literal methods `{ n: 2, twice(x: number) { return x * this.n; } }` (`this` is the object); an
  object literal can implement an interface with methods.
- Object literal getters and setters: `this` is the object; the literal implements interfaces that
  declare the accessors (`get x(): T` / `set x(v: T)`), not ones that declare `x` as a property (read as a field).
  They are own properties: `Object.keys`, `JSON.stringify`, spread and destructuring see them (calling the getter),
  `console.log` prints `[Getter]`. A getter without a setter is read-only.

```ts
interface Temperature { celsius: number; get fahrenheit(): number; set fahrenheit(v: number) }
const t: Temperature = {
  celsius: 20,
  get fahrenheit() { return this.celsius * 1.8 + 32; },
  set fahrenheit(f) { this.celsius = (f - 32) / 1.8; },
};
t.fahrenheit += 18;
const p = { x: 3, y: 4, get length(): number { return Math.hypot(this.x, this.y); } };
console.log(t.celsius, p.length, JSON.stringify(p), p);  // 30 5 {"x":3,"y":4,"length":5} { x: 3, y: 4, length: [Getter] }
```

```ts e7-reject
interface HasArea { readonly area: number }
const sq: HasArea = { get area() { return 9; } };
```
- `?.` (also `a?.[i]`), `??`, `??=`, `!` (checked at run time), `as T` (upcasts; class downcasts are checked at
  run time).
- Narrowing works on `=== null`, `!== undefined`, `== null`, truthiness, `instanceof`, early returns, and
  property paths (`if (this.head !== null) this.head.next ...`).
- Comparing values of unrelated types (`"1" === 1`) is a compile error, as in TypeScript.
- `typeof x` is evaluated at compile time; do not use it for runtime type dispatch.

## 4. Functions and classes

- Parameters need type annotations (arrow functions passed as callbacks infer them). Default, optional and rest
  parameters work (also in arrow functions). A recursive function needs an explicit return type.
- An empty array literal needs a type: `const xs: string[] = []` (`c ? [x] : []` takes the other branch's type).
- Classes: fields (typed or initialized), constructor parameter properties, methods, static members,
  `extends` (derived constructors call `super(...)` first), `super.method()`, `implements` (structural),
  `instanceof`. Overrides must keep the same signature. Fields without an initializer start as
  `0` / `false` / `null`, optional fields as `undefined`.
- `get x()` / `set x(v)` (also static, overridable, `super.x`), `#private` fields and methods, `abstract`
  classes, methods and properties (a concrete subclass declares an abstract property as a field), and
  `static { }` blocks (run with the static initializers, in order):

```ts
abstract class Shape {
  abstract area(): number;
  get label(): string { return `area ${this.area()}`; }
}
class Square extends Shape {
  private _side: number;
  static created = 0;
  static { Square.created = -1; }
  constructor(side: number) { super(); this._side = side; Square.created++; }
  get side(): number { return this._side; }
  set side(s: number) { if (s < 0) throw new RangeError("negative"); this._side = s; }
  area(): number { return this._side ** 2; }
}
const sq = new Square(3);
sq.side += 1;
console.log(sq.label, sq.side, Square.created, JSON.stringify(sq));
```

- `#private` fields are not own properties: `Object.keys`, `JSON.stringify` and `console.log` leave them out, as in
  Node.js.
- Arrow-function fields can capture `this` and are the way to pass "methods" around.
- Generics: functions, `const` arrow functions, classes (with generic methods), interfaces and type aliases,
  constraints (`<T extends Shape>`), defaults (`<T = number>`), explicit type arguments (`f<number>(x)`) and
  inference from arguments and the expected type. Each type argument list gets its own compiled copy;
  static members are shared by all instantiations. Errors in generic code name the instantiation
  (`... (in first<string>)`):

```ts
function groupBy<T, K>(xs: T[], key: (x: T) => K): Map<K, T[]> {
  const m = new Map<K, T[]>();
  for (const x of xs) {
    const g = m.get(key(x));
    if (g) g.push(x);
    else m.set(key(x), [x]);
  }
  return m;
}
class Stack<T> {
  private items: T[] = [];
  push(x: T): Stack<T> { this.items.push(x); return this; }
  pop(): T | undefined { return this.items.pop(); }
  map<U>(f: (x: T) => U): Stack<U> {
    const s = new Stack<U>();
    for (const x of this.items) s.push(f(x));
    return s;
  }
}
interface Result<T> { ok: boolean; value: T | null }
const ok = <T,>(value: T): Result<T> => ({ ok: true, value });
console.log(groupBy(["a", "bb", "cc"], (s) => s.length), new Stack<number>().push(1).push(2).map((n) => `#${n}`).pop());
console.log(ok(42), ok("s").value);
```

## 5. Modules and host functions

- `import { a, b as c } from "./x"` (extension optional), `import x from "./x"` (default), `import * as ns from "./x"`
  (functions, constants, classes, enums and types through `ns.`), `import type`, `import "./x"` (side effects only).
- `export function/const/class/interface/type/enum`, `export { a, b as c }`, `export default ...`, re-exports
  (`export { a } from "./x"`, `export * from "./x"`, `export * as ns from "./x"`). No circular imports.
- Built-in modules: `e7:crypto`, `e7:encoding`, `e7:json`, `e7:test` (see stdlib.md and testing.md).
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
| E7's own checks (out of bounds, failed `as`, limits) | — | not catchable; `finally` blocks do not run |
| `Date` local time | host time zone | UTC |
| `toUpperCase` / `localeCompare` | full Unicode / locale | Latin, Greek, Cyrillic / code units |
| `console.log` of an arrow function | `[Function: f]` | `[Function (anonymous)]` |

Runtime errors (out of bounds, `null` access, failed `!`/`as`, integer division by zero, `throw`,
`abort(msg)`, failed `JSON.parse` validation, exhausted instruction budget or memory) stop the current call; the
host receives the message and a stack trace such as `at total (cart.ts:12)`. The VM stays usable for the next call.
