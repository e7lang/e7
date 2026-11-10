`solution.ts` parses the diagnostic log the app attaches to support tickets (a lazy generator pipeline:
`lines`, `parseLog`, `atLeast`, `take`, plus `countBySource` and `firstErrors`). Two things support asked for:

**1. Multi-line entries.** Stack traces and payloads are logged on indented lines after the entry, and today
we drop them. A line that starts with a space or a tab and is not blank after trimming is a *continuation
line*: when it comes right after an entry's line or after another continuation line of that entry, it is
appended to that entry's message as `"\n" + line.trim()`. A continuation line anywhere else (first line,
after a blank line or after a line that was skipped) is skipped. `line` stays the number of the entry's first line.

Split the parsing so it can run on any line source, and keep `parseLog` as it is for callers:

```ts
export function parseLines(src: IterableIterator<string>): Generator<LogEntry>
export function parseLog(text: string): Generator<LogEntry>   // = parseLines(lines(text)), same signature as today
```

`parseLines` must stay lazy: it yields an entry as soon as it reads the first line that does not continue
it (or when the source ends), and never reads further ahead than that.

**2. Bursts.** To spot incidents we want to group entries that come in quick succession:

```ts
export function bursts(entries: IterableIterator<LogEntry>, gapSeconds: number): Generator<LogEntry[]>
export function errorBursts(text: string, gapSeconds: number): string[]
```

- `bursts`: consecutive entries form a burst; an entry joins the current burst when its time
  (`Date.parse(entry.time)`) is at least the previous entry's time and at most `gapSeconds` later (exactly
  `gapSeconds` still joins). Otherwise it starts a new burst. Bursts are yielded lazily: a burst is yielded
  as soon as the entry that starts the next one has been read (or the entries end). No entries: nothing is yielded.
- `errorBursts(text, gapSeconds)`: for every burst of `parseLog(text)` that has at least one `ERROR` entry,
  in order: `"lines <first>-<last>: <n> entries, <e> errors"`, where `<first>` / `<last>` are the `line` of
  the burst's first and last entry; write `1 entry` and `1 error` in the singular
  (`"lines 7-7: 1 entry, 1 error"`).

Logs without indented lines must give exactly the same results as today. `solution.test.ts` has the
current tests.
