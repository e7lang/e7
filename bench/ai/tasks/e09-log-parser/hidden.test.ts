import { assertEqual } from "e7:test";
import { bursts, countBySource, errorBursts, firstErrors, parseLines, parseLog, take } from "./solution.ts";
import type { LogEntry } from "./solution.ts";

const SIMPLE = [
  "2024-05-01T10:00:00Z INFO [auth] user signed in",
  "2024-05-01T10:00:02Z DEBUG [sync] 12 items queued",
  "",
  "garbage line",
  "2024-05-01T10:00:05Z ERROR [sync] upload failed: 503",
  "2024-05-01T10:00:09Z WARN [auth] token expires soon",
  "2024-05-01T10:01:00Z ERROR [db] disk full",
].join("\n");

const TRACE = [
  "2024-05-01T10:00:00Z INFO [app] started",
  "2024-05-01T10:00:01Z ERROR [sync] crash: TypeError",
  "    at upload (sync.ts:10)",
  "\tat run (main.ts:3)",
  "2024-05-01T10:00:02Z INFO [sync] restarted",
  "   ",
  "  orphan continuation after a blank line",
  "not a log line",
  "  continuation of a skipped line",
  "2024-05-01T10:00:30Z ERROR [db] slow query",
  "  select * from t",
].join("\r\n");

function* source(items: string[], pulled: number[]): Generator<string> {
  for (const s of items) {
    pulled[0]++;
    yield s;
  }
}

function messages(entries: LogEntry[]): string[] {
  return entries.map((e) => e.message);
}

export function testSingleLineLogsUnchanged(): void {
  const entries = [...parseLog(SIMPLE)];
  assertEqual(entries.map((e) => e.line), [1, 2, 5, 6, 7]);
  assertEqual(countBySource(SIMPLE), "auth=2 db=1 sync=1");
  assertEqual(firstErrors(SIMPLE, 5), ["5: upload failed: 503", "7: disk full"]);
}

export function testContinuationLines(): void {
  const entries = [...parseLog(TRACE)];
  assertEqual(entries.length, 4);
  assertEqual(entries[1], {
    line: 2,
    time: "2024-05-01T10:00:01Z",
    level: "ERROR",
    source: "sync",
    message: "crash: TypeError\nat upload (sync.ts:10)\nat run (main.ts:3)",
  });
  assertEqual(entries[3].message, "slow query\nselect * from t");
}

export function testOrphanContinuationsAreSkipped(): void {
  const entries = [...parseLog(TRACE)];
  assertEqual(messages(entries), ["started", "crash: TypeError\nat upload (sync.ts:10)\nat run (main.ts:3)", "restarted", "slow query\nselect * from t"]);
  assertEqual([...parseLog("  leading continuation\n2024-05-01T10:00:00Z INFO [a] x")].length, 1);
}

export function testFirstErrorsWithTraces(): void {
  assertEqual(firstErrors(TRACE, 1), ["2: crash: TypeError\nat upload (sync.ts:10)\nat run (main.ts:3)"]);
  assertEqual(countBySource(TRACE, "ERROR"), "db=1 sync=1");
}

export function testParseLinesIsLazy(): void {
  const pulled = [0];
  const items = [
    "2024-05-01T10:00:00Z INFO [a] one",
    "  more of one",
    "2024-05-01T10:00:01Z INFO [a] two",
    "2024-05-01T10:00:02Z INFO [a] three",
    "2024-05-01T10:00:03Z INFO [a] four",
  ];
  const first = [...take(parseLines(source(items, pulled)), 1)];
  assertEqual(messages(first), ["one\nmore of one"]);
  assertEqual(pulled[0], 3); // the entry is complete once line 3 is seen
  pulled[0] = 0;
  assertEqual(messages([...take(parseLines(source(items, pulled)), 2)]), ["one\nmore of one", "two"]);
  assertEqual(pulled[0], 4);
}

export function testLastEntryIsYielded(): void {
  const pulled = [0];
  const items = ["2024-05-01T10:00:00Z WARN [a] last", "  tail 1", "  tail 2"];
  assertEqual(messages([...parseLines(source(items, pulled))]), ["last\ntail 1\ntail 2"]);
  assertEqual(pulled[0], 3);
}

export function testBursts(): void {
  const groups = [...bursts(parseLog(SIMPLE), 5)];
  assertEqual(groups.map((g) => g.map((e) => e.line)), [[1, 2, 5, 6], [7]]);
  assertEqual([...bursts(parseLog(SIMPLE), 3)].map((g) => g.length), [3, 1, 1]);
  assertEqual([...bursts(parseLog(""), 10)].length, 0);
}

export function testBurstGapBoundaryAndClockGoingBack(): void {
  const log = [
    "2024-05-01T10:00:00Z INFO [a] 1",
    "2024-05-01T10:00:10Z INFO [a] 2", // exactly 10 s later: same burst
    "2024-05-01T10:00:20.001Z INFO [a] 3", // 10.001 s: new burst
    "2024-05-01T10:00:19Z INFO [a] 4", // earlier than the previous entry: new burst
    "2024-05-01T10:00:19Z INFO [a] 5", // same time: same burst
  ].join("\n");
  assertEqual([...bursts(parseLog(log), 10)].map((g) => messages(g)), [["1", "2"], ["3"], ["4", "5"]]);
}

export function testBurstsAreLazy(): void {
  const pulled = [0];
  const items = [
    "2024-05-01T10:00:00Z INFO [a] 1",
    "2024-05-01T10:00:01Z INFO [a] 2",
    "2024-05-01T10:05:00Z INFO [a] 3",
    "2024-05-01T10:05:01Z INFO [a] 4",
    "2024-05-01T10:09:00Z INFO [a] 5",
  ];
  const first = [...take(bursts(parseLines(source(items, pulled)), 60), 1)];
  assertEqual(first.map((g) => messages(g)), [["1", "2"]]);
  assertEqual(pulled[0], 4); // line 4 completes entry 3, which ends the first burst
}

export function testErrorBursts(): void {
  assertEqual(errorBursts(SIMPLE, 5), ["lines 1-6: 4 entries, 1 error", "lines 7-7: 1 entry, 1 error"]);
  assertEqual(errorBursts(SIMPLE, 60), ["lines 1-7: 5 entries, 2 errors"]);
  assertEqual(errorBursts(TRACE, 5), ["lines 1-5: 3 entries, 1 error", "lines 10-10: 1 entry, 1 error"]);
  assertEqual(errorBursts("2024-05-01T10:00:00Z INFO [a] fine", 5), []);
}
