import { assertEqual } from "e7:test";
import { countBySource, firstErrors, parseLog, take } from "./solution.ts";

const LOG = [
  "2024-05-01T10:00:00Z INFO [auth] user signed in",
  "2024-05-01T10:00:02Z DEBUG [sync] 12 items queued",
  "",
  "garbage line",
  "2024-05-01T10:00:05Z ERROR [sync] upload failed: 503",
  "2024-05-01T10:00:09Z WARN [auth] token expires soon",
  "2024-05-01T10:01:00Z ERROR [db] disk full",
].join("\n");

export function testParse(): void {
  const entries = [...parseLog(LOG)];
  assertEqual(entries.length, 5);
  assertEqual(entries[2], { line: 5, time: "2024-05-01T10:00:05Z", level: "ERROR", source: "sync", message: "upload failed: 503" });
}

export function testCountBySource(): void {
  assertEqual(countBySource(LOG), "auth=2 db=1 sync=1");
  assertEqual(countBySource(LOG, "DEBUG"), "auth=2 db=1 sync=2");
}

export function testFirstErrors(): void {
  assertEqual(firstErrors(LOG, 1), ["5: upload failed: 503"]);
  assertEqual(firstErrors(LOG, 5), ["5: upload failed: 503", "7: disk full"]);
}

export function testTakeIsLazy(): void {
  let pulled = 0;
  function* counting(): Generator<number> {
    for (let i = 0; i < 100; i++) {
      pulled++;
      yield i;
    }
  }
  assertEqual([...take(counting(), 3)], [0, 1, 2]);
  assertEqual(pulled, 3);
}
