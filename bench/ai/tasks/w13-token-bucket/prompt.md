We want client-side throttling in the app (so we stop hammering the API before the server answers 429). Please
write `solution.ts` (an E7 script) with two classes:

```ts
export class TokenBucket {
  readonly capacity: number;
  readonly refillPerSecond: number;
  constructor(capacity: number, refillPerSecond: number, startMs: number);
  tryRemove(nowMs: number, tokens?: number): boolean;   // tokens defaults to 1
  available(nowMs: number): number;
  retryAfterMs(nowMs: number, tokens?: number): number; // tokens defaults to 1
  isFull(nowMs: number): boolean;
}
export class RateLimiter {
  constructor(capacity: number, refillPerSecond: number);
  get size(): number;
  allow(key: string, nowMs: number): boolean;
  prune(nowMs: number): number;
}
```

Times are milliseconds passed in by the caller (no clock in the script).

**TokenBucket.** It starts full (`capacity` tokens) at `startMs`. Tokens refill continuously at
`refillPerSecond` (may be fractional, e.g. 0.5) and never exceed `capacity`; the token count itself can be
fractional. Every method first brings the bucket up to `nowMs`: add `(nowMs - last) * refillPerSecond / 1000`
tokens and remember `nowMs` as the last time. If `nowMs` is earlier than the last time, nothing is added and the
last time stays as it was (clocks can jump back).

- `tryRemove(nowMs, tokens = 1)`: if at least `tokens` are available, take them and return true; otherwise
  return false and take nothing.
- `available(nowMs)`: whole tokens available (rounded down).
- `retryAfterMs(nowMs, tokens = 1)`: 0 if `tokens` are available now, otherwise how many ms until they will
  be, rounded up: `Math.ceil((tokens - current) * 1000 / refillPerSecond)`. It does not take tokens.
- `isFull(nowMs)`: the bucket holds `capacity` tokens.
- `capacity` must be a positive integer and `refillPerSecond` greater than 0, otherwise the constructor throws
  `new Error("invalid bucket config")`. A `tokens` argument that is not an integer from 1 to `capacity` throws
  `new Error("invalid token count")`.

**RateLimiter** keeps one bucket per key (all with the same `capacity` and `refillPerSecond`; the constructor
validates them the same way). `allow(key, nowMs)` creates the key's bucket on first use (full, starting at
`nowMs`) and then takes one token from it. `size` is the number of keys with a bucket. `prune(nowMs)` deletes
the buckets that are full at `nowMs` (they are equivalent to new ones) and returns how many it deleted.

`solution.test.ts` has a couple of examples.
