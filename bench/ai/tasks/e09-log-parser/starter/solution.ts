// Parses the diagnostic log the app uploads with a support ticket. Logs can be large, so everything
// is a lazy generator pipeline: nothing reads more lines than the consumer asks for.
//
// Line format: "<ISO time> <LEVEL> [<source>] <message>", e.g.
//   2024-05-01T10:00:00Z WARN [sync] retrying upload
// Lines that do not match (including blank lines) are skipped.

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

export function* parseLog(text: string): Generator<LogEntry> {
  let n = 0;
  for (const line of lines(text)) {
    n++;
    const entry = parseLine(line, n);
    if (entry !== null) yield entry;
  }
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
