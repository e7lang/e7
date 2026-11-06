# E7 standard library

Everything below behaves like JavaScript unless noted (it is checked against Node.js in E7's test suite).
The exact signatures of the E7-specific parts are in [e7.d.ts](e7.d.ts) (a copy of `lib/e7.d.ts`).
Anything not listed here (Node.js and browser APIs, `Intl`, `Proxy`, `Reflect`, `WeakMap`, ...) does not exist.

## Globals

`console.log/info/warn/error`, `print(...)`, `abort(message)` (ends the call), `parseInt`, `parseFloat`,
`isNaN`, `isFinite`, `Number(x)`, `String(x)`, `Boolean(x)`, `BigInt(x)`, `encodeURIComponent`,
`decodeURIComponent`, `encodeURI`, `decodeURI`, `NaN`, `Infinity`, `Object.keys(obj)`.

## Numbers and integers

- `Math.*` (all functions and constants; `Math.random()` is seedable by the host).
- `Number.isInteger/isSafeInteger/isFinite/isNaN/parseFloat/parseInt` and constants;
  `x.toString(radix)`, `toFixed`, `toPrecision`, `toExponential`.
- Integer conversions `i8() u8() i16() u16() i32() u32() i64() u64() int()` (wrap modulo 2^n, truncate fractions).
- Bit helpers: `rotl32 rotr32 rotl64 rotr64 clz32 clz64 ctz32 ctz64 popcnt bswap16 bswap32 bswap64`,
  `mulhi64` / `imulhi64` (high 64 bits of a 128-bit product), `idiv` / `imod` (truncating division on numbers).
- `bigint`: all operators, `BigInt.asUintN/asIntN`, `toString(radix)`.

```ts
// FNV-1a with machine integers
function fnv1a(data: Uint8Array): u32 {
  let h: u32 = 0x811c9dc5;
  for (const b of data) {
    h ^= u32(b);
    h *= 16777619;
  }
  return h;
}
console.log(fnv1a(new TextEncoder().encode("hello")).toString(16)); // 4f9f2cab
```

## Strings

`length charAt charCodeAt codePointAt at indexOf lastIndexOf includes startsWith endsWith slice substring substr
toUpperCase toLowerCase trim trimStart trimEnd padStart padEnd repeat split replace replaceAll concat
localeCompare match matchAll search`, `String.fromCharCode/fromCodePoint`, template strings. `split` /
`replace` / `replaceAll` take a string or a regular expression; with a string pattern the replacement can be a
function `(match, offset, string) => string`. Strings are UTF-16 like JavaScript; `for (const ch of s)` iterates code points.

## Regular expressions (0.3)

ES2024 regular expressions without the `v` flag, as in Node.js: literals `/.../dgimsuy` (checked when compiling),
`new RegExp(pattern, flags?)` (a bad pattern throws a catchable `SyntaxError`), `test exec lastIndex source flags
global ...`, and `s.match / matchAll / replace / replaceAll / split / search` with a `RegExp`. `exec` gives
`RegExpExecArray | null`: a `string[]` with `index`, `input`, `groups`. The named groups of a **literal** are typed
(`/(?<y>\d{4})/.exec(s)?.groups?.y` is `string | undefined`); a pattern from `new RegExp` has only the indexed groups
`m[1]`. A replacement function gets `(match, p1, ..., pn, offset, input, groups)` for a literal, `(match)` for a
pattern built at run time. Matching steps count against the instruction budget (ReDoS ends the call).

## Arrays, Map, Set

- Arrays: `length` (writable: `a.length = n`), `push pop shift unshift at slice splice concat indexOf lastIndexOf
  includes join reverse fill sort map filter forEach reduce reduceRight some every find findIndex findLast
  findLastIndex flatMap flat`, `Array.from`, `Array.of`, `new Array<T>(n)`, spread. `flat(depth)` needs a
  constant depth. Default `sort()` compares as strings
  (pass `(a, b) => a - b` for numbers); sorting is stable.
- Objects: `Object.keys`, `Object.values` / `Object.entries` (for types whose properties all have one
  type), `Object.assign` (a literal target, or a target type that has every source property), `Object.freeze`.
- `Map<K, V>` / `Set<T>`: `size get set has delete clear forEach keys values entries`, `for...of`, spread,
  copy construction (`new Map(other)`).

## Bytes and text

`Uint8Array` (`length set subarray slice fill indexOf includes join reverse at forEach`, `Uint8Array.from/of`),
`ArrayBuffer`, `DataView` (all `get/set` methods, both endiannesses), `TextEncoder`, `TextDecoder` (UTF-8).

## JSON and Date

- `JSON.stringify(value, null, indent?)`; `JSON.parse(text) as T` validates against `T`
  (`T` may be an object type, array, tuple, primitive, nullable/optional fields, or `Map<string, V>`); a mismatch
  ends the call. For untrusted input use `tryParse(text) as T | null` from `e7:json` (below).
- `Date` (UTC): `Date.now/parse/UTC`, constructors, all `get*`/`getUTC*`, `getTime`, `setTime`,
  `toISOString`, `toJSON`, `toString`, `toUTCString`. Other `set*` methods are missing: compute with `Date.UTC`.

## `e7:json`

`tryParse(text) as T | null` — the same typed, validating parse as `JSON.parse(text) as T` (same `T`), but a
syntax error or a value of the wrong shape gives `null` instead of ending the call. Use it for untrusted input.
`lastError()` returns why the last `tryParse` failed (`"expected a number at $.amount"`, `"missing property at $.id"`,
`"SyntaxError: unexpected end of input at position 8"`), or `""` after a success.

- The `as T | null` is required (`tryParse(text)` alone or `as T` without `| null` is a compile error).
- A document that is just `null` counts as a mismatch, even when `T` is nullable; check `lastError()` if you must
  tell a JSON `null` apart from bad input. Nested nullable values work as usual.
- Nesting deeper than 256 levels gives `null`. Running out of instruction budget or memory is still an error.
- On Node.js (tests with the shim) only syntax errors are detected; the shape is checked by E7 only.

```ts
import { tryParse, lastError } from "e7:json";

interface Order { id: string; items: { sku: string; qty: number }[]; note?: string }

const ok = tryParse('{"id": "A1", "items": [{"sku": "pen", "qty": 2}]}') as Order | null;
console.log(ok !== null ? ok.items[0].qty : -1, JSON.stringify(lastError())); // 2 ""

const wrong = tryParse('{"id": "A1", "items": [{"sku": "pen", "qty": "2"}]}') as Order | null;
console.log(wrong === null, lastError()); // true expected a number at $.items[0].qty

const broken = tryParse('{"id": ') as Order | null;
console.log(broken === null, lastError()); // true SyntaxError: unexpected end of input at position 7

const prices = tryParse('{"pen": 1.5, "pad": 3}') as Map<string, number> | null;
console.log(prices?.get("pad")); // 3
```

```ts e7-reject
import { tryParse } from "e7:json";
interface Order { id: string }
const o = tryParse("{}") as Order; // error: write tryParse(text) as Order | null
```

## `e7:encoding`

`hexEncode/hexDecode`, `base64Encode/base64Decode`, `base64UrlEncode/base64UrlDecode`, `base32Encode/base32Decode`,
`utf8Encode/utf8Decode`, `pemDecode/pemEncode`, `concatBytes(...parts)`, `bytesEqual(a, b)`.

## `e7:crypto`

All algorithms run as native code (BearSSL, Monocypher); scripts only prepare inputs.

| Purpose | Functions |
|---|---|
| Hashes | `md5 sha1 sha224 sha256 sha384 sha512 sha3_224 sha3_256 sha3_384 sha3_512 keccak256`, `hash(name, data)` |
| MAC / KDF | `hmac(name, key, data)` (key may be a `KeyHandle`), `hkdf`, `pbkdf2` |
| AEAD / ciphers | `aesGcmEncrypt/Decrypt`, `chacha20Poly1305Encrypt/Decrypt` (decrypt returns `null` on auth failure), `aesCbcEncrypt/Decrypt`, `aesCtr` |
| Signatures | `ed25519PublicKey/Sign/Verify`, `ecdsaSign/Verify/PublicKey` (P-256/384/521), `rsaSign/Verify` (PKCS#1 v1.5, PSS) |
| Key agreement | `x25519` |
| Misc | `randomBytes(n)`, `timingSafeEqual(a, b)`, `crc32` |
| Types | `KeyHandle` (a key held by the host), `HashName` (a string such as `"sha256"`) |

Hash names: `"md5" "sha1" "sha224" "sha256" "sha384" "sha512" "sha3-224" "sha3-256" "sha3-384" "sha3-512" "keccak256"`.

```ts
import { hmac, sha256, ed25519PublicKey, ed25519Sign, ed25519Verify, timingSafeEqual } from "e7:crypto";
import { hexEncode, hexDecode, utf8Encode, base64UrlEncode } from "e7:encoding";

const mac = hmac("sha256", utf8Encode("secret"), utf8Encode("message"));
console.log(hexEncode(mac));

// compare MACs in constant time
const expected = hexDecode(hexEncode(mac));
console.log(timingSafeEqual(mac, expected));

// Ed25519 (a deterministic seed for the demo; real keys come from the host)
const seed = sha256(utf8Encode("demo"));
const sig = ed25519Sign(seed, utf8Encode("pay 100"));
console.log(base64UrlEncode(sig).length, ed25519Verify(ed25519PublicKey(seed), utf8Encode("pay 100"), sig));
```

## `e7:test`

`assert`, `assertEqual`, `assertNotEqual`, `assertClose`, `assertThrows`, `fail` — see [testing.md](testing.md).
