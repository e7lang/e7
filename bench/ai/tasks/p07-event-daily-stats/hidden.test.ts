import { assertEqual } from "e7:test";
import { dailyStats } from "./solution.ts";

// Expected values were produced by running original.ts on Node.js.

export function testOffsetMovesEventToNextUtcDayAndGapFilled(): void {
  const log = '# export of 2024-03-01..2024-03-04\n2024-03-01T08:00:00Z view user=u1\n2024-03-01T08:00:05Z view user=u2\n2024-03-01T08:01:00Z click user=u1\n2024-03-01T08:03:00Z purchase user=u1 amount=19.99\n2024-03-01T23:30:00-02:00 view user=u3\n\n2024-03-02T01:00:00Z click user=u3\n2024-03-04T12:00:00Z refund user=u1 amount=5\n2024-03-04T12:00:00Z view\tuser=u_9';
  assertEqual(dailyStats(log, '', ''), '{"days":[{"day":"2024-03-01","views":2,"clicks":1,"purchases":1,"refunds":0,"users":2,"revenue":"19.99","ctr":"50.0%"},{"day":"2024-03-02","views":1,"clicks":1,"purchases":0,"refunds":0,"users":1,"revenue":"0.00","ctr":"100.0%"},{"day":"2024-03-03","views":0,"clicks":0,"purchases":0,"refunds":0,"users":0,"revenue":"0.00","ctr":"-"},{"day":"2024-03-04","views":1,"clicks":0,"purchases":0,"refunds":1,"users":2,"revenue":"-5.00","ctr":"0.0%"}],"skippedLines":[]}');
}

export function testRangeFrom(): void {
  const log = '# export of 2024-03-01..2024-03-04\n2024-03-01T08:00:00Z view user=u1\n2024-03-01T08:00:05Z view user=u2\n2024-03-01T08:01:00Z click user=u1\n2024-03-01T08:03:00Z purchase user=u1 amount=19.99\n2024-03-01T23:30:00-02:00 view user=u3\n\n2024-03-02T01:00:00Z click user=u3\n2024-03-04T12:00:00Z refund user=u1 amount=5\n2024-03-04T12:00:00Z view\tuser=u_9';
  assertEqual(dailyStats(log, '2024-03-02', ''), '{"days":[{"day":"2024-03-02","views":1,"clicks":1,"purchases":0,"refunds":0,"users":1,"revenue":"0.00","ctr":"100.0%"},{"day":"2024-03-03","views":0,"clicks":0,"purchases":0,"refunds":0,"users":0,"revenue":"0.00","ctr":"-"},{"day":"2024-03-04","views":1,"clicks":0,"purchases":0,"refunds":1,"users":2,"revenue":"-5.00","ctr":"0.0%"}],"skippedLines":[]}');
}

export function testRangeTo(): void {
  const log = '# export of 2024-03-01..2024-03-04\n2024-03-01T08:00:00Z view user=u1\n2024-03-01T08:00:05Z view user=u2\n2024-03-01T08:01:00Z click user=u1\n2024-03-01T08:03:00Z purchase user=u1 amount=19.99\n2024-03-01T23:30:00-02:00 view user=u3\n\n2024-03-02T01:00:00Z click user=u3\n2024-03-04T12:00:00Z refund user=u1 amount=5\n2024-03-04T12:00:00Z view\tuser=u_9';
  assertEqual(dailyStats(log, '', '2024-03-01'), '{"days":[{"day":"2024-03-01","views":2,"clicks":1,"purchases":1,"refunds":0,"users":2,"revenue":"19.99","ctr":"50.0%"}],"skippedLines":[]}');
}

export function testRangeWithoutEvents(): void {
  const log = '# export of 2024-03-01..2024-03-04\n2024-03-01T08:00:00Z view user=u1\n2024-03-01T08:00:05Z view user=u2\n2024-03-01T08:01:00Z click user=u1\n2024-03-01T08:03:00Z purchase user=u1 amount=19.99\n2024-03-01T23:30:00-02:00 view user=u3\n\n2024-03-02T01:00:00Z click user=u3\n2024-03-04T12:00:00Z refund user=u1 amount=5\n2024-03-04T12:00:00Z view\tuser=u_9';
  assertEqual(dailyStats(log, '2024-03-05', '2024-03-09'), '{"days":[],"skippedLines":[]}');
}

export function testEmptyLog(): void {
  assertEqual(dailyStats('', '', ''), '{"days":[],"skippedLines":[]}');
}

export function testCrLfAndTrailingSpace(): void {
  const log = '2024-05-10T09:00:00Z view user=a\r\n2024-05-10T09:00:01Z click user=a  \r\n   \r\n# note';
  assertEqual(dailyStats(log, '', ''), '{"days":[{"day":"2024-05-10","views":1,"clicks":1,"purchases":0,"refunds":0,"users":1,"revenue":"0.00","ctr":"100.0%"}],"skippedLines":[]}');
}

export function testAmountFormats(): void {
  const log = '2024-05-10T09:00:00Z purchase user=a amount=5\n2024-05-10T09:00:00Z purchase user=b amount=0.1\n2024-05-10T09:00:00Z purchase user=c amount=1.999\n2024-05-10T09:00:00Z purchase user=d amount=.5\n2024-05-10T09:00:00Z purchase user=e\n2024-05-10T09:00:00Z refund user=a amount=12.30';
  assertEqual(dailyStats(log, '', ''), '{"days":[{"day":"2024-05-10","views":0,"clicks":0,"purchases":3,"refunds":1,"users":3,"revenue":"-7.20","ctr":"-"}],"skippedLines":[3,4]}');
}

export function testMalformedLines(): void {
  const log = '2024-05-10T09:00:00Z View user=a\n2024-05-10T09:00:00Z view user=\n2024-05-10T09:00:00Z view user=a-b\n2024-05-10T09:00:00Z view user=a extra\nnot-a-date view user=a\n2024-05-10T09:00:00Z view user=ok\n2024-05-10T09:00:00Z  click   user=ok';
  assertEqual(dailyStats(log, '', ''), '{"days":[{"day":"2024-05-10","views":1,"clicks":1,"purchases":0,"refunds":0,"users":1,"revenue":"0.00","ctr":"100.0%"}],"skippedLines":[1,2,3,4,5]}');
}

export function testDateOnlyAndYearEnd(): void {
  const log = '2023-12-31T23:59:59Z view user=x\n2024-01-02 click user=y\n2024-01-01T00:00:00.000+00:00 view user=x';
  assertEqual(dailyStats(log, '', ''), '{"days":[{"day":"2023-12-31","views":1,"clicks":0,"purchases":0,"refunds":0,"users":1,"revenue":"0.00","ctr":"0.0%"},{"day":"2024-01-01","views":1,"clicks":0,"purchases":0,"refunds":0,"users":1,"revenue":"0.00","ctr":"0.0%"},{"day":"2024-01-02","views":0,"clicks":1,"purchases":0,"refunds":0,"users":1,"revenue":"0.00","ctr":"-"}],"skippedLines":[]}');
}

export function testLeapDay(): void {
  assertEqual(dailyStats('2024-02-28T12:00:00Z view user=p\n2024-03-01T12:00:00Z view user=p', '', ''), '{"days":[{"day":"2024-02-28","views":1,"clicks":0,"purchases":0,"refunds":0,"users":1,"revenue":"0.00","ctr":"0.0%"},{"day":"2024-02-29","views":0,"clicks":0,"purchases":0,"refunds":0,"users":0,"revenue":"0.00","ctr":"-"},{"day":"2024-03-01","views":1,"clicks":0,"purchases":0,"refunds":0,"users":1,"revenue":"0.00","ctr":"0.0%"}],"skippedLines":[]}');
}

export function testCtrRounding(): void {
  const log = '2024-06-01T00:00:00Z view user=a\n2024-06-01T00:00:00Z view user=b\n2024-06-01T00:00:00Z view user=c\n2024-06-01T00:00:00Z click user=a\n2024-06-01T00:00:00Z click user=b';
  assertEqual(dailyStats(log, '', ''), '{"days":[{"day":"2024-06-01","views":3,"clicks":2,"purchases":0,"refunds":0,"users":3,"revenue":"0.00","ctr":"66.7%"}],"skippedLines":[]}');
}
