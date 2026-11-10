The desktop app checks license keys offline before it activates. Please write `solution.ts` (an E7 script) with:

```ts
export interface LicenseCheck { ok: boolean; error: string; normalized: string; edition: string }
export function checkLicenseKey(input: string): LicenseCheck
export function makeLicenseKey(body: string): string
```

A key is 20 characters from the alphabet `23456789ABCDEFGHJKLMNPQRSTUVWXYZ` (no 0, 1, I or O), written in four
groups of five: `PABCD-2345E-FGHJ6-789KQ`. The value of a character is its index in that alphabet (`2` is 0,
`Z` is 31). The 20th character is a check character: multiply the value of each of the first 19 characters by
its 1-based position, add everything up, and take the alphabet character at index `sum % 32`. The first
character is the edition: `S` = `"standard"`, `P` = `"pro"`, `E` = `"enterprise"`.

`checkLicenseKey`: people type keys by hand, so first remove all dashes and spaces and uppercase the rest. Then
check, in this order, and stop at the first failure:

1. not exactly 20 characters → `"wrong length"`
2. a character outside the alphabet → `invalid character "<c>" at position <n>` for the first such character
   (`<c>` as it is after uppercasing, `<n>` 1-based among the 20 characters), e.g. `invalid character "O" at position 7`
3. wrong check character → `"bad checksum"`
4. first character is not an edition letter → `"unknown edition"`

On success: `{ ok: true, error: "", normalized: "PABCD-2345E-FGHJ6-789KQ", edition: "pro" }` (`normalized` is
the canonical dashed uppercase form). On failure `ok` is false and `normalized` and `edition` are `""`.

`makeLicenseKey(body)` takes the first 19 characters (exactly 19 alphabet characters, uppercase, no dashes),
appends the check character and returns the dashed key. Anything else throws `new Error("invalid key body")`.

`solution.test.ts` has a couple of examples.
