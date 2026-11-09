The app needs to mint and check short-lived JWTs offline (HS256 only). Please write `solution.ts` (an E7 script)
with:

```ts
export interface JwtResult { valid: boolean; error: string; claimsJson: string }
export function signJwt(claimsJson: string, secret: string): string
export function verifyJwt(token: string, secret: string, nowSeconds: number): JwtResult
```

`signJwt` builds a standard compact JWT: the header is exactly `{"alg":"HS256","typ":"JWT"}`, the payload is
`claimsJson` exactly as given (do not re-serialize it), each is UTF-8 encoded and base64url encoded without
padding, and the signature is the base64url (no padding) HMAC-SHA256 of `"<header>.<payload>"` keyed with the
UTF-8 bytes of `secret`.

`verifyJwt` returns `{ valid: true, error: "", claimsJson: <the decoded payload text> }` for a good token.
Otherwise `valid` is false, `claimsJson` is `""` and `error` is the first of these that applies, checked in
this order:

- `"malformed token"`: the token is not exactly three `.`-separated parts; a part is empty or is not unpadded
  base64url (only `A–Z a–z 0–9 - _` are allowed); the header is not a JSON object with a string `alg`; or the
  payload is not a JSON object (when it has `exp` or `nbf`, they must be numbers).
- `"unsupported algorithm"`: `alg` is anything other than `"HS256"` (including `"none"`).
- `"invalid signature"`: the signature does not match. Compare in constant time.
- `"expired"`: the payload has `exp` and `nowSeconds >= exp`.
- `"not yet valid"`: the payload has `nbf` and `nowSeconds < nbf`.

`solution.test.ts` has a couple of examples.
