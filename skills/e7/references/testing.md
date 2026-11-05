# Testing E7 scripts

Put tests in files named `*.test.ts` next to the code, export one function per test whose name starts with
`test` (no parameters), and run `e7 test`. Each test runs in a fresh VM with its own instruction budget and
memory limit, so tests cannot affect each other and an endless loop fails instead of hanging.

```ts fragment
// pricing.test.ts
import { assert, assertEqual, assertClose, assertThrows } from "e7:test";
import { totalCents, parseCoupon } from "./pricing.ts";

export function testTotalAddsTax(): void {
  assertEqual(totalCents([199, 301], 0.1), 550);
}

export function testCouponIsCaseInsensitive(): void {
  assertEqual(parseCoupon(" welcome10 "), 10);
}

export function testUnknownCouponIsRejected(): void {
  assertThrows(() => { parseCoupon("FREE"); }, "unknown coupon");
}
```

```bash
e7 test                      # every **/*.test.ts under the current directory
e7 test src/pricing.test.ts  # specific files or directories
e7 test --filter Coupon      # only tests whose name (or file) contains the text
e7 test --json               # machine-readable results
```

Output: `✓ pricing.test.ts › testTotalAddsTax (0.2 ms)` per test, failures with the message, the stack trace
and the test's output, then `N passed, M failed`. The exit code is 1 when a test fails.

## Assertions (`e7:test`)

| Function | Fails when |
|---|---|
| `assert(cond, msg?)` | `cond` is false |
| `assertEqual(actual, expected, msg?)` | values differ structurally (arrays, objects, class instances, Maps, Sets, `Uint8Array`, Dates...) |
| `assertNotEqual(actual, expected, msg?)` | values are structurally equal |
| `assertClose(actual, expected, tolerance = 1e-9, msg?)` | numbers differ by more than the tolerance |
| `assertThrows(fn, expectedMessage?)` | `fn()` does not raise a runtime error (or its message lacks the text) |
| `fail(msg?)` | always |

`assertThrows` is the only place where a script error can be observed without ending the test, which makes
it the way to test input validation that uses `throw`.

A test passes when it returns normally (or returns `true` if its return type is `boolean`). Tests are ordinary
TypeScript too: with `e7.d.ts` in the `tsconfig.json`, editors type-check them.

## Testing exported functions with known vectors

Most E7 scripts implement a protocol (a signature scheme, a checksum, a token format). Test them against
published test vectors or values produced by the server-side implementation, for example RFC 4231 for
HMAC-SHA256 — this catches encoding mistakes (hex vs base64, UTF-8, canonical ordering) that unit logic tests miss.
