We roll features out to a percentage of users, and the mobile apps must put every user in the same bucket as
the server does, so the bucketing has to be bit-exact. Please write `solution.ts` (an E7 script) with:

```ts
export interface Flag { key: string; enabled: boolean; rolloutPercent: number; allowUsers: string[]; denyUsers: string[] }
export interface Variant { name: string; weight: number }
export function fnv1a32(text: string): number
export function bucketOf(flagKey: string, userId: string): number
export function isEnabled(flag: Flag, userId: string): boolean
export function variantFor(experimentKey: string, userId: string, variants: Variant[]): string
```

- `fnv1a32(text)`: 32-bit FNV-1a over the UTF-8 bytes of `text` (offset basis `0x811c9dc5`, prime `16777619`;
  for each byte: xor, then multiply modulo 2^32). Returns the unsigned value, e.g. `fnv1a32("a") === 3826002220`.
- `bucketOf(flagKey, userId)`: `fnv1a32(flagKey + ":" + userId) % 10000`, so 0..9999.
- `isEnabled(flag, userId)`, first matching rule wins:
  1. flag not `enabled` → false (for everyone, even allow-listed users);
  2. user in `denyUsers` → false;
  3. user in `allowUsers` → true;
  4. otherwise true when `bucketOf(flag.key, userId) < Math.round(flag.rolloutPercent * 100)`.
     `rolloutPercent` is 0..100 with at most two decimals, so 12.5 means buckets 0..1249.
- `variantFor(experimentKey, userId, variants)`: let `total` be the sum of the (non-negative integer) weights
  and `point = fnv1a32(experimentKey + ":" + userId) % total`. Walk the variants in order adding up weights; the
  answer is the first variant whose running total is greater than `point` (so a weight-0 variant is never
  chosen). If `total` is 0 (or there are no variants) throw `new Error("no variants")`.

`solution.test.ts` has a few examples.
