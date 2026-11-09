`solution.ts` creates and checks the offline license keys of our desktop edition (the version 1 format is
described at the top of the file). Sales now sells add-on features and licenses with more than 255 seats, so we
need a second key format. Please extend `solution.ts`:

```ts
export interface LicenseInfo {
  product: number;
  expiresDay: number;
  seats: number;
  features?: number; // new: bit n set = feature n (0..31); an unsigned 32-bit value
}

export function makeKey(info: LicenseInfo, secret: string): string
export function parseKey(key: string, secret: string): LicenseInfo | null
export function checkKey(key: string, secret: string, product: number, today: number): string
export function hasFeature(key: string, secret: string, bit: number): boolean   // new
export function describeKey(key: string, secret: string): string               // new
```

**Version 2 keys**: 15 bytes, big-endian, base32 (same alphabet, no padding) = 24 characters, written in
groups of 4 joined by `-` like version 1 keys:

| Bytes | Field |
|---|---|
| 1 | version: 2 |
| 2 | product id |
| 2 | expiry day (days since 2020-01-01) |
| 2 | seats |
| 4 | features |
| 4 | checksum: the same FNV-1a 32 as version 1, over UTF-8(secret) followed by the 11 bytes above |

- `makeKey` writes a version 2 key when `info.features` is set (even to 0) or when `seats` is above 255;
  otherwise it writes the same version 1 key as today. Errors (thrown, checked in this order): `bad product`,
  `bad expiry`, `bad seats` (an integer 0..255 for version 1, 0..65535 for version 2), `bad features` (an
  integer 0..4294967295).
- `parseKey` accepts both lengths (after removing dashes and spaces, case-insensitive, as today). Version 2
  results always have `features` (as an unsigned number, e.g. 2147483657 when bits 0, 3 and 31 are set);
  version 1 results do not have it. A 24-character key whose first byte is not 2 gives `null`.
- `checkKey` results, in this order: `"malformed"` (not 16 or 24 base32 characters), `"unsupported version"`
  (24 characters whose first byte is not 2), `"bad checksum"`, `"wrong product"`, `"expired"`, `"valid"`.
- `hasFeature`: throws `bad feature bit` unless `bit` is an integer 0..31; otherwise true only for a key that
  `parseKey` accepts, is version 2 and has that bit set (it does not check product or expiry).
- `describeKey`: `"invalid key"` when `parseKey` gives null, otherwise
  `"product 42, 5 seats, until 2025-12-31"` (`1 seat` for one seat; the expiry day as a UTC `YYYY-MM-DD`
  date), and for version 2 keys `, features 0,3,31` appended (set bits ascending) or `, features none`.

Version 1 keys must keep working exactly as today. `solution.test.ts` has the current tests.
