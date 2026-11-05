---
name: e7
description: Write, fix and review code in E7, the strict TypeScript subset that runs on the e7vm bytecode VM (github.com/e7lang/e7). Use this skill whenever the user works on E7 scripts, .e7 files, .ts files that import "e7:crypto", "e7:encoding", "e7:json" or "e7:test", code compiled with e7c or run with e7 / e7vm / @e7lang/e7, scripts embedded in Android/iOS/Windows/macOS/Linux apps through e7vm, or asks for signing / verification / algorithm scripts "for E7" — even if they just say "TypeScript". E7 looks like TypeScript but rejects many TypeScript features (any, regex, async, union types, index signatures...), so code written as ordinary TypeScript usually fails to compile; this skill explains what to write instead and how to check it.
---

# Writing E7

E7 is a **strict subset of TypeScript** compiled to bytecode and run inside apps by e7vm. Every valid E7
program is also valid TypeScript, but most TypeScript is *not* valid E7. Write code for E7 deliberately:
the compiler resolves every name and type statically, so anything dynamic (`any`, runtime type tests,
exceptions, regex, promises, dynamic keys) is rejected.

Typical E7 code is an algorithm a host app calls: request signing, signature/webhook verification, license
checks, price or rule calculations, data transforms. Scripts are compiled on a server (`e7c`), shipped as
`.e7b` bytecode, verified and run by the app.

## The rules that matter most

Write it the E7 way from the start; each left-hand construct is a compile error.

| TypeScript habit | Write in E7 |
|---|---|
| `any`, `unknown`, `object` | a concrete type, an `interface`, or `T \| null` |
| `throw "text"`, `catch (e) { e.message }` | `throw new Error("text")`; narrow first: `if (e instanceof Error) e.message` (`finally` works) |
| generic methods in interfaces, `<K extends keyof T>`, `x instanceof Box` (generic class) | generic functions, classes, interfaces and aliases work (compiled per type argument list); use a generic class or function instead |
| `string \| number`, discriminated unions | one interface with a `kind` field (string or enum) and optional fields |
| `{ [key: string]: V }`, `Record<K, V>`, `Partial`, `Pick` | `Map<string, V>`, or write the interface out |
| regex `/.../`, `RegExp` | string methods: `split`, `indexOf`, `startsWith`, `slice`, `charCodeAt`, `replaceAll` (string patterns) |
| `async`/`await`, `Promise`, `setTimeout`, `fetch` | synchronous code; the host does I/O and passes data in |
| `for (k in obj)`, `delete`, `in` | `Object.keys(obj)`, or use a `Map` |
| `class A { get x() }` implementing `interface I { x: number }` | declare the accessor in the interface: `get x(): number` (interfaces read properties as fields) |
| `interface Node { next: Node \| null }` (recursive interface) | a `class` (classes may refer to themselves) |
| `var`, `==` | `let`/`const`, `===` (`x == null` is allowed: null or undefined) |
| `JSON.parse(text)` | `JSON.parse(text) as T` (validated against `T`; a shape mismatch ends the call and cannot be caught, a syntax error is a catchable `SyntaxError`); for untrusted input `tryParse(text) as T \| null` from `e7:json` (`null` on bad input, `lastError()` says why) |
| `arr.map(obj.method)` | `arr.map(x => obj.method(x))` (methods are not values) |
| `Buffer`, `crypto.subtle`, `require("crypto")`, `fs`, `process` | `Uint8Array`, `e7:crypto`, `e7:encoding`, or a host function |
| `Int32Array`, `Float64Array` | `number[]`, `u32[]`, or `DataView` |

Behaviour that differs from JavaScript at run time:

- **Out-of-bounds reads are runtime errors** (`arr[i]`, `s[i]`, `bytes[i]`), not `undefined`. Check `i < arr.length` first.
- **`number` is exactly JavaScript's double.** Machine integers `i8 u8 i16 u16 i32 u32 i64 u64` (and `int` = `i64`)
  are opt-in: they wrap on overflow, `/` truncates, and they never mix implicitly with `number` — convert with
  `u32(x)`, `i64(x)`, `Number(x)`. Write `u32(a + b)` style when the same code must also run on Node.
- `Date` uses UTC only; only `setTime` of the `set*` methods exists.
- `typeof x` is a compile-time constant (no runtime type tests); narrow with `!== null`, truthiness or `instanceof`.

For the complete list (destructuring limits, class rules, operators, every standard-library method) read
[references/language.md](references/language.md). For the built-in modules read
[references/stdlib.md](references/stdlib.md) — its `e7.d.ts` is the authoritative API.

## Shape of a script

```ts
import { hmac } from "e7:crypto";
import { hexEncode, utf8Encode } from "e7:encoding";

// Exported functions are what the host app calls. Parameters and results must be host types:
// number, boolean, string, i8..u64, Uint8Array, KeyHandle (and void for results).
export function signRequest(method: string, path: string, body: string, secret: string, timestamp: number): string {
  const canonical = [method.toUpperCase(), path, String(timestamp), body].join("\n");
  return hexEncode(hmac("sha256", utf8Encode(secret), utf8Encode(canonical)));
}

// main() is optional; the `e7` CLI and the playground call it.
export function main(): string {
  return signRequest("post", "/v1/pay", '{"amount":100}', "s3cr3t", 1700000000);
}
```

- Use objects, arrays, classes and `Map`s freely *inside* the script; only exported signatures are limited to host types.
  To pass structured data across the boundary, use a JSON string and `JSON.parse(text) as T`.
- Secrets: accept a `KeyHandle` (from `e7:crypto`) instead of key bytes when the key lives in the app's Keychain/Keystore;
  `hmac`, `ecdsaSign`, etc. accept it directly.
- Host functions are declared, not imported: `declare function deviceId(): string;` (the app registers it).
- Top-level code runs once when the module loads; module-level variables keep their values between calls.

[references/embedding.md](references/embedding.md) covers host types, host functions, `KeyHandle`, budgets and how apps
load scripts. Read it when writing the host side (Kotlin, Swift, C#, C, JavaScript) or the exported API.

## Workflow: always compile-check

The compiler is fast and its messages are precise (with "did you mean ...?" suggestions), so check every
script you write or edit instead of trusting your memory of the subset:

1. Write the script (`.ts`, or `.e7` for files you don't want TypeScript tooling to pick up).
2. Check it — use whichever tool is available:
   - `e7c --check script.ts` (native toolchain: prebuilt from github.com/e7lang/e7/releases, or Homebrew/Scoop)
   - `e7-js check script.ts` from the npm package `@e7lang/e7` (WebAssembly build, any OS with Node.js 18+;
     install it from npm or from the `.tgz` attached to each GitHub release)
3. Fix every error and re-check until it is clean. If an error names a feature from the table above, rewrite
   the construct the E7 way instead of fighting the checker.
4. Run it: `e7 script.ts` (calls `main()`, or `--call name`), or `e7-js run script.ts`.
5. Test it: put tests in `*.test.ts` and run `e7 test`. See [references/testing.md](references/testing.md).
6. Ship it: `e7c script.ts -o script.e7b` (add `--sign key.seed` if the app requires signed modules).

If no E7 toolchain is installed and you cannot install one, say so, keep to the rules above, and suggest the
playground at https://e7lang.github.io/playground to verify the code.

## Error handling patterns

`try`/`catch` exists (since 0.2), but it only catches errors that Node.js also throws: `throw`, `JSON.parse`
syntax errors, `RangeError`s and the like from the standard library. E7's own checks — out-of-bounds reads,
`as T` / `JSON.parse(...) as T` shape mismatches, integer division by zero — and budget / memory limits still end
the call. The catch variable is `unknown`; narrow it before use:

```ts
function safeDiv(a: bigint, b: bigint): string {
  try {
    return (a / b).toString();
  } catch (e) {
    if (e instanceof RangeError) return "division by zero";
    throw e;
  }
}

export function main(): string {
  return safeDiv(10n, 2n) + ", " + safeDiv(1n, 0n);
}
```

For expected failures, prefer returning a value:

```ts
interface ParseResult { ok: boolean; value: number; error: string }

function parseAmount(text: string): ParseResult {
  const n = Number(text);
  if (text.trim() === "" || !Number.isFinite(n)) return { ok: false, value: 0, error: `not a number: "${text}"` };
  if (n < 0) return { ok: false, value: 0, error: "amount must not be negative" };
  return { ok: true, value: n, error: "" };
}

export function main(): string {
  const r = parseAmount("12.50");
  return r.ok ? `amount ${r.value}` : `error: ${r.error}`;
}
```

Use `T | null` for "not found", a result interface for "failed with a reason", and `throw new Error(msg)` for
errors a caller may handle with `catch`. `abort(msg)` cannot be caught. An error no `catch` handles reaches the host
with a stack trace. Remember `JSON.parse(text) as T` and `x!` also stop the call when the data does not match.

For JSON that may be malformed or of the wrong shape (requests, webhooks, files, anything untrusted), use
`tryParse` from `e7:json`. It validates exactly like `JSON.parse(text) as T` but returns `null` instead of ending
the call; `lastError()` gives the reason. The `as T | null` is required:

```ts
import { tryParse, lastError } from "e7:json";

interface Payment { id: string; amount: number; currency?: string }

export function handle(body: string): string {
  const p = tryParse(body) as Payment | null;
  if (p === null) return "rejected: " + lastError(); // e.g. "expected a number at $.amount"
  return `ok ${p.id} ${p.amount} ${p.currency ?? "EUR"}`;
}

export function main(): string {
  return [handle('{"id": "p1", "amount": 5}'), handle('{"id": "p2", "amount": "5"}'), handle("{oops")].join("\n");
}
```

A document that is just `null` also gives `null` (with an error); budget and memory exhaustion are still errors.

## More

- [references/language.md](references/language.md) — complete language rules and the TypeScript features E7 rejects, with rewrites
- [references/stdlib.md](references/stdlib.md) — standard library, `e7:crypto`, `e7:encoding`, `e7:json`, integer helpers, and `e7.d.ts`
- [references/testing.md](references/testing.md) — `e7:test` assertions and the `e7 test` runner
- [references/embedding.md](references/embedding.md) — exported functions, host functions, `KeyHandle`, limits, host-side APIs
- [examples/](examples/) — complete, compile-checked scripts: request signing, webhook verification, typed JSON, tests
