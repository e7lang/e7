The app downloads plugins and must pick versions compatible with what it supports, using the same rules as
our registry. Please write `solution.ts` (an E7 script) with:

```ts
export interface SemVer { major: number; minor: number; patch: number; prerelease: string[] }
export function parseVersion(text: string): SemVer
export function compareVersions(a: string, b: string): number
export function satisfies(version: string, range: string): boolean
export function maxSatisfying(versions: string[], range: string): string | null
```

**Versions** follow SemVer 2.0: `MAJOR.MINOR.PATCH`, optionally `-prerelease` (dot-separated identifiers) and
`+build` (dot-separated identifiers). Surrounding whitespace and one leading `v` are allowed. The three numbers
are digits only, without leading zeros (`0` is fine, `01` is not). Identifiers are non-empty and use only
`0-9 A-Z a-z -`; a purely numeric prerelease identifier must not have leading zeros (build identifiers may). `parseVersion` returns the
numbers and the prerelease identifiers (build metadata is dropped); anything invalid throws
`new Error("invalid version: <text>")` with the text as given. All the other functions throw the same error
when they get an invalid version.

**Ordering** (`compareVersions` returns -1, 0 or 1): compare major, minor, patch numerically; a version with a
prerelease is lower than the same version without; prerelease identifiers are compared left to right —
numeric ones numerically, others as strings (code-unit order), numeric lower than non-numeric, and if all
compared ones are equal the longer list is greater. Build metadata is ignored (`1.0.0+a` equals `1.0.0+b`).
So `1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta < 1.0.0-beta.2 < 1.0.0-beta.11 < 1.0.0-rc.1 < 1.0.0`.

**Ranges**: one or more comparator sets separated by `||` (any set may match); a set is one or more
comparators separated by spaces (all must match). Comparators (no space between operator and version):

- `1.2.3` or `=1.2.3`: equal; `>1.2.3`, `>=1.2.3`, `<1.2.3`, `<=1.2.3`
- `~1.2.3`: `>=1.2.3 <1.3.0`
- `^1.2.3`: `>=1.2.3 <2.0.0`; `^0.2.3`: `>=0.2.3 <0.3.0`; `^0.0.3`: `>=0.0.3 <0.0.4`
- `*`: any version

Versions in comparators are full versions (prerelease allowed, e.g. `>=2.0.0-rc.1`). A range that is empty, has
an empty side of `||`, or contains anything else throws `new Error("invalid range: <range>")`.

**Prereleases** (as npm does): a version that has a prerelease tag satisfies a set only if, besides meeting all
comparators, at least one comparator in that set has a prerelease tag on the same `MAJOR.MINOR.PATCH`. So
`1.3.0-beta` does not satisfy `^1.2.0`, but `1.2.0-beta.3` satisfies `>=1.2.0-beta.1`. (`*` alone never matches
a prerelease.)

`maxSatisfying` returns the highest version in the list that satisfies the range, as written in the list (the
first one if several are equal), or `null`.

`solution.test.ts` has a few examples.
