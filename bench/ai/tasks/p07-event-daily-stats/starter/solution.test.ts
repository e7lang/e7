import { assertEqual } from "e7:test";
import { dailyStats } from "./solution.ts";

export function testOneDay(): void {
  const log = '2024-03-01T10:00:00Z view user=u17\n2024-03-01T10:01:00Z click user=u17\n2024-03-01T10:02:13Z purchase user=u17 amount=19.99\n2024-03-01T11:00:00Z view user=u18';
  assertEqual(dailyStats(log, '', ''), '{"days":[{"day":"2024-03-01","views":2,"clicks":1,"purchases":1,"refunds":0,"users":2,"revenue":"19.99","ctr":"50.0%"}],"skippedLines":[]}');
}

export function testBadLineSkipped(): void {
  assertEqual(dailyStats('2024-03-01T10:00:00Z view user=u1\n2024-03-01T10:00:00Z share user=u1', '', ''), '{"days":[{"day":"2024-03-01","views":1,"clicks":0,"purchases":0,"refunds":0,"users":1,"revenue":"0.00","ctr":"0.0%"}],"skippedLines":[2]}');
}
