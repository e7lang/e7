`solution.ts` is the E7 script our app uses to sign requests to the partner API (`signRequest` builds the
`Authorization` header). Two changes are coming from the backend team, please add both to `solution.ts`:

**1. HMAC-SHA512.** Partners are moving to SHA-512, so signing gets an algorithm parameter that defaults to
the current behaviour:

```ts
export type SignatureAlg = "sha256" | "sha512";
export function canonicalRequest(req: ApiRequest, timestamp: number, alg: SignatureAlg = "sha256"): string
export function signRequest(req: ApiRequest, keyId: string, secretHex: string, timestamp: number, alg: SignatureAlg = "sha256"): string
```

With `"sha512"`, the last line of the canonical request is the lowercase hex SHA-512 of the body (instead of
SHA-256), the signature is the lowercase hex HMAC-SHA512 of the canonical request, and the header starts with
`E7-HMAC-SHA512` instead of `E7-HMAC-SHA256`. Everything else in the format stays exactly as it is now. Calls
without the new argument must give exactly the same results as today.

**2. Verifying requests.** The backend now also calls the app (push callbacks) and signs those requests with
the same scheme. Add:

```ts
export function verifyRequest(authorization: string, req: ApiRequest, keys: Map<string, string>, nowSeconds: number): string
```

`keys` maps a key id to its secret (hex). The result is `""` when the request is valid, otherwise the first
problem found, checked in this order:

1. `"malformed"` if there is no space in `authorization`. The part before the first space is the scheme.
2. `"unknown algorithm"` if the scheme is not `E7-HMAC-SHA256` or `E7-HMAC-SHA512`.
3. `"malformed"` if a parameter has no `=`, if `keyId`, `ts` or `sig` is missing, or if `ts` is not a non-empty
   string of digits `0`-`9`. The parameters (after the first space) are separated by `,`, each is
   `name=value` split at its first `=`; no trimming. They may come in any order; other parameters (such as
   `headers`) are ignored.
4. `"unknown key"` if `keyId` is not in `keys`.
5. `"stale"` if `ts` is more than 300 seconds away from `nowSeconds`, in either direction (exactly 300 is fine).
6. `"bad signature"` unless `sig` equals the signature `signRequest` would compute for `req` with that key's
   secret, that `ts` and the scheme's algorithm. Compare in constant time.

`signRequest`, `canonicalRequest` and `canonicalQuery` must keep working as before (existing callers and
their tests rely on them). `solution.test.ts` has the current tests.
