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
localeCompare`, `String.fromCharCode/fromCodePoint`, template strings. `split` / `replace` take string
patterns only (no regex). Strings are UTF-16 like JavaScript; `for (const ch of s)` iterates code points.

## Arrays, Map, Set

- Arrays: `length` (writable: `a.length = n`), `push pop shift unshift at slice splice concat indexOf lastIndexOf
  includes join reverse fill sort map filter forEach reduce reduceRight some every find findIndex findLast
  findLastIndex`, `Array.from`, `Array.of`, `new Array<T>(n)`, spread. Default `sort()` compares as strings
  (pass `(a, b) => a - b` for numbers); sorting is stable.
- `Map<K, V>` / `Set<T>`: `size get set has delete clear forEach keys values entries`, `for...of`, spread,
  copy construction (`new Map(other)`).

## Bytes and text

`Uint8Array` (`length set subarray slice fill indexOf includes join reverse at forEach`, `Uint8Array.from/of`),
`ArrayBuffer`, `DataView` (all `get/set` methods, both endiannesses), `TextEncoder`, `TextDecoder` (UTF-8).

## JSON and Date

- `JSON.stringify(value, null, indent?)`; `JSON.parse(text) as T` validates against `T`
  (`T` may be an object type, array, tuple, primitive, nullable/optional fields, or `Map<string, V>`).
- `Date` (UTC): `Date.now/parse/UTC`, constructors, all `get*`/`getUTC*`, `getTime`, `setTime`,
  `toISOString`, `toJSON`, `toString`, `toUTCString`. Other `set*` methods are missing: compute with `Date.UTC`.

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
