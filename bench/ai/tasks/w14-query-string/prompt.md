Deep links and API calls in the app pass parameters in URL query strings, and we keep getting encoding bugs.
Please write `solution.ts` (an E7 script) with:

```ts
export interface QueryParam { key: string; value: string }
export function parseQuery(query: string): QueryParam[]
export function buildQuery(params: QueryParam[]): string
export function getParam(query: string, key: string): string | null
export function setParam(url: string, key: string, value: string): string
```

- **`parseQuery`**: a leading `?` is ignored. Split on `&`, skipping empty pieces. Each piece is split at its
  first `=` into key and value (no `=` means the value is `""`). In keys and values `+` means a space and
  `%XX` sequences are UTF-8 percent-encoding. Keep every pair, in order, duplicates included. If a key or value
  has a `%` that is not followed by two hex digits, or the bytes are not valid UTF-8, throw
  `new Error("invalid encoding: <piece>")` with the whole raw piece (e.g. `invalid encoding: q=100%`).
- **`buildQuery`**: `key=value` pairs joined with `&`, in order, no leading `?` (`""` for no params). Keys and
  values are encoded the same way: ASCII letters, digits and `- . _ ~` stay as they are, a space becomes `+`,
  and every other character is UTF-8 encoded with each byte written as `%XX` in uppercase hex (so `!'()*` are
  encoded too, unlike `encodeURIComponent`). Always write the `=`, even for an empty value.
- **`getParam`**: the value of the first pair with exactly that key (after decoding), or `null`.
- **`setParam(url, key, value)`**: the URL's query is between the first `?` and the first `#` (the fragment,
  `#...`, may be absent and is kept unchanged at the end). Set the first pair with this key to `value`, remove any
  later pairs with the same key, or append the pair at the end if the key is absent. Other pairs keep their
  order. The new query is written with `buildQuery` (so the existing pairs come out normalized, e.g. `%20`
  becomes `+`), and the result always has a `?`.

`solution.test.ts` has a few examples.
