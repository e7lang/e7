// Parses the diagnostic log the app uploads with a support ticket. Logs can be large, so everything
// is a lazy generator pipeline: nothing reads more lines than the consumer asks for.
//
// Line format: "<ISO time> <LEVEL> [<source>] <message>", e.g.
//   2024-05-01T10:00:00Z WARN [sync] retrying upload
// Lines that do not match (including blank lines) are skipped. An indented line (space or tab) right
// after an entry continues that entry's message (stack traces, multi-line payloads).

export type Level = "DEBUG" | "INFO" | "WARN" | "ERROR";

export interface LogEntry {
  line: number; // 1-based
  time: string;
  level: Level;
  source: string;
  message: string;
}

const LEVELS: Level[] = ["DEBUG", "INFO", "WARN", "ERROR"];

function toLevel(text: string): Level | null {
  for (const level of LEVELS) if (level === text) return level;
  return null;
}

function levelRank(level: Level): number {
  return LEVELS.indexOf(level);
}

function parseLine(text: string, line: number): LogEntry | null {
  const sp1 = text.indexOf(" ");
  if (sp1 <= 0) return null;
  const sp2 = text.indexOf(" ", sp1 + 1);
  if (sp2 < 0) return null;
  const level = toLevel(text.slice(sp1 + 1, sp2));
  if (level === null || text[sp2 + 1] !== "[") return null;
  const close = text.indexOf("]", sp2 + 2);
  if (close < 0 || close === sp2 + 2) return null;
  const message = text.slice(close + 1).trim();
  return { line, time: text.slice(0, sp1), level, source: text.slice(sp2 + 2, close), message };
}

export function* lines(text: string): Generator<string> {
  let start = 0;
  while (start <= text.length) {
    let end = text.indexOf("\n", start);
    if (end < 0) end = text.length;
    let line = text.slice(start, end);
    if (line.endsWith("\r")) line = line.slice(0, line.length - 1);
    yield line;
    start = end + 1;
  }
}

function isContinuation(line: string): boolean {
  return (line.startsWith(" ") || line.startsWith("\t")) && line.trim() !== "";
}

// Yields each entry once the line after it shows that it is complete (or the input ends).
export function* parseLines(src: IterableIterator<string>): Generator<LogEntry> {
  let n = 0;
  let pending: LogEntry | null = null;
  for (const line of src) {
    n++;
    if (pending !== null && isContinuation(line)) {
      pending.message += "\n" + line.trim();
      continue;
    }
    if (pending !== null) yield pending;
    pending = parseLine(line, n);
  }
  if (pending !== null) yield pending;
}

export function parseLog(text: string): Generator<LogEntry> {
  return parseLines(lines(text));
}

export function* atLeast(entries: IterableIterator<LogEntry>, min: Level): Generator<LogEntry> {
  for (const e of entries) if (levelRank(e.level) >= levelRank(min)) yield e;
}

export function* take<T>(items: IterableIterator<T>, n: number): Generator<T> {
  if (n <= 0) return;
  let i = 0;
  for (const item of items) {
    yield item;
    if (++i >= n) return;
  }
}

// Groups consecutive entries: an entry joins the current burst when it comes at most `gapSeconds`
// after the previous entry (and not before it).
export function* bursts(entries: IterableIterator<LogEntry>, gapSeconds: number): Generator<LogEntry[]> {
  let current: LogEntry[] = [];
  let lastTime = NaN;
  for (const e of entries) {
    const t = Date.parse(e.time);
    const diff = t - lastTime;
    if (current.length > 0 && !(diff >= 0 && diff <= gapSeconds * 1000)) {
      yield current;
      current = [];
    }
    current.push(e);
    lastTime = t;
  }
  if (current.length > 0) yield current;
}

// "lines 5-7: 3 entries, 2 errors" for every burst with at least one error.
export function errorBursts(text: string, gapSeconds: number): string[] {
  const out: string[] = [];
  for (const burst of bursts(parseLog(text), gapSeconds)) {
    const errors = burst.filter((e) => e.level === "ERROR").length;
    if (errors === 0) continue;
    const first = burst[0].line;
    const last = burst[burst.length - 1].line;
    const entries = `${burst.length} ${burst.length === 1 ? "entry" : "entries"}`;
    out.push(`lines ${first}-${last}: ${entries}, ${errors} ${errors === 1 ? "error" : "errors"}`);
  }
  return out;
}

// "auth=2 sync=1": entries at or above `min` per source, sources sorted by name.
export function countBySource(text: string, min: Level = "INFO"): string {
  const counts = new Map<string, number>();
  for (const e of atLeast(parseLog(text), min)) counts.set(e.source, (counts.get(e.source) ?? 0) + 1);
  return [...counts.keys()].sort().map((s) => `${s}=${counts.get(s) ?? 0}`).join(" ");
}

// "<line>: <message>" of the first `n` errors.
export function firstErrors(text: string, n: number): string[] {
  return [...take(atLeast(parseLog(text), "ERROR"), n)].map((e) => `${e.line}: ${e.message}`);
}
