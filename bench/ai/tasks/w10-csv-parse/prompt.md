Users import contacts and price lists from spreadsheets, and the app parses the CSV on the device. Please
write `solution.ts` (an E7 script) with:

```ts
export function parseCsv(text: string): string[][]
export function toCsv(rows: string[][]): string
export function parseCsvObjects(text: string): Map<string, string>[]
```

**`parseCsv`** (RFC 4180 style):

- Fields are separated by `,`, records by `\n` or `\r\n`. A lone `\r` is an ordinary character.
- A field that *starts* with `"` is quoted: it ends at the next single `"`; inside it `""` stands for one `"`,
  and `,`, `\r` and `\n` are ordinary characters. A `"` anywhere else in an unquoted field is just a character
  (`5" disk` stays `5" disk`).
- Nothing is trimmed: spaces belong to the field.
- An empty input gives `[]`. One line break at the very end of the input does not start another record, but an
  empty line in the middle is a record with one empty field (`[""]`). A trailing `,` means a last empty field.
- Errors (throw `new Error(...)`), with 1-based physical line numbers (line breaks inside quoted fields count):
  - a quoted field that is never closed → `unterminated quote starting on line <n>` (the line of its opening quote)
  - anything other than `,`, a line break or the end of input right after a closing quote →
    `unexpected character after quote on line <n>`

**`toCsv`** writes rows back: a field is wrapped in quotes (with each `"` doubled) when it contains `,`, `"`,
`\r` or `\n`, or starts or ends with a space; otherwise it is written as is. Fields are joined with `,` and
rows with `\r\n`, with no line break after the last row.

**`parseCsvObjects`** uses the first record as the header and returns one `Map` per following record, header
name → value, in header order. No records gives `[]`. A record whose field count differs from the header throws
`new Error("record <n> has <m> fields, expected <k>")` where `n` is the 1-based record number (the header is
record 1).

`solution.test.ts` has a few examples.
