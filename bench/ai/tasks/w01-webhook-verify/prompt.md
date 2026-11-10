Our partner signs the webhooks they send us, and the app has to check them before acting on one. Please write
`solution.ts` (an E7 script) with:

```ts
export interface VerifyResult { ok: boolean; reason: string }
export function verifyWebhook(header: string, body: string, secretHex: string, nowSeconds: number): VerifyResult
```

The signature header looks like `t=1700000000,v1=5257a8...,v1=9f86d0...`: comma-separated `key=value` pairs.
`t` is the Unix time in seconds when it was sent. Each `v1` is a lowercase hex HMAC-SHA256 of the string
`"<t>.<body>"` keyed with the shared secret (given to you as hex). During a key rotation there can be several
`v1` entries; the webhook is valid if any of them matches. Ignore other keys (they may add `v2` later) and
spaces around pairs.

Result: `{ ok: true, reason: "" }` when valid. Otherwise `ok` is false and `reason` is, checked in this order:

- `"malformed header"` if a pair has no `=` (an empty header, or an empty piece between commas, counts as a
  pair without `=`),
- `"missing timestamp"` if there is no `t` or it is not a whole number of seconds,
- `"stale"` if `t` is more than 300 seconds away from `nowSeconds` (in either direction),
- `"no signature"` if there is no `v1`,
- `"signature mismatch"` if no `v1` matches.

Compare signatures in constant time. `solution.test.ts` has a couple of examples.
