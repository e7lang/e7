import { assertEqual } from "e7:test";
import { authorize } from "./solution.ts";

// Expected values were produced by running original.ts on Node.js.

const POLICY = '{' +
  '"roles":{"viewer":{},"editor":{"inherits":["viewer"]},"admin":{"inherits":["editor","billing"]},"billing":{"inherits":["viewer"]},"loop":{"inherits":["loop2"]},"loop2":{"inherits":["loop","viewer"]}},' +
  '"users":{"ann":["admin"],"bob":["editor"],"cyd":["viewer","ghost"],"dan":["loop"],"eve":[]},' +
  '"rules":[' +
  '{"effect":"allow","permission":"docs:read","roles":["viewer"]},' +
  '{"effect":"allow","permission":"docs:*","roles":["editor"]},' +
  '{"effect":"deny","permission":"docs:delete","roles":["editor"],"when":[{"type":"hours","from":22,"to":6}]},' +
  '{"effect":"allow","permission":"*:delete","when":[{"type":"owner"}]},' +
  '{"effect":"allow","permission":"billing.invoices:*","roles":["billing"],"when":[{"type":"ipPrefix","prefixes":["10.0.","192.168.1."]}]},' +
  '{"effect":"deny","permission":"*","roles":["ghost"]},' +
  '{"effect":"allow","permission":"status:read"}' +
  ']}';

export function testInheritedRolesSorted(): void {
  const request = '{"userId":"ann","permission":"docs:write","at":"2024-05-01T09:15:00Z","ip":"8.8.8.8"}';
  assertEqual(authorize(POLICY, request), '{"decision":"allow","reason":"allowed by rule 2","roles":["admin","billing","editor","viewer"]}');
}

export function testDenyAtNightWins(): void {
  const request = '{"userId":"bob","permission":"docs:delete","at":"2024-05-01T23:10:00Z","ip":"8.8.8.8"}';
  assertEqual(authorize(POLICY, request), '{"decision":"deny","reason":"denied by rule 3","roles":["editor","viewer"]}');
}

export function testDenyWrapsPastMidnight(): void {
  const request = '{"userId":"bob","permission":"docs:delete","at":"2024-05-02T05:59:59Z","ip":"8.8.8.8"}';
  assertEqual(authorize(POLICY, request), '{"decision":"deny","reason":"denied by rule 3","roles":["editor","viewer"]}');
}

export function testHoursEndIsExclusive(): void {
  const request = '{"userId":"bob","permission":"docs:delete","at":"2024-05-02T06:00:00Z","ip":"8.8.8.8"}';
  assertEqual(authorize(POLICY, request), '{"decision":"allow","reason":"allowed by rule 2","roles":["editor","viewer"]}');
}

export function testTimeZoneOffsetConvertedToUtc(): void {
  const request = '{"userId":"bob","permission":"docs:delete","at":"2024-05-02T01:30:00+04:00","ip":"8.8.8.8"}';
  assertEqual(authorize(POLICY, request), '{"decision":"allow","reason":"allowed by rule 2","roles":["editor","viewer"]}');
}

export function testOwnerMayDelete(): void {
  const request = '{"userId":"eve","permission":"photos:delete","ownerId":"eve","at":"2024-05-01T12:00:00Z","ip":"8.8.8.8"}';
  assertEqual(authorize(POLICY, request), '{"decision":"allow","reason":"allowed by rule 4","roles":[]}');
}

export function testNotOwner(): void {
  const request = '{"userId":"eve","permission":"photos:delete","ownerId":"ann","at":"2024-05-01T12:00:00Z","ip":"8.8.8.8"}';
  assertEqual(authorize(POLICY, request), '{"decision":"deny","reason":"no matching rule","roles":[]}');
}

export function testIpPrefix(): void {
  const request = '{"userId":"ann","permission":"billing.invoices:export","at":"2024-05-01T12:00:00Z","ip":"192.168.1.20"}';
  assertEqual(authorize(POLICY, request), '{"decision":"allow","reason":"allowed by rule 5","roles":["admin","billing","editor","viewer"]}');
}

export function testDotInPatternIsLiteral(): void {
  const request = '{"userId":"ann","permission":"billingXinvoices:export","at":"2024-05-01T12:00:00Z","ip":"10.0.0.1"}';
  assertEqual(authorize(POLICY, request), '{"decision":"deny","reason":"no matching rule","roles":["admin","billing","editor","viewer"]}');
}

export function testUndefinedRoleIsIgnored(): void {
  const request = '{"userId":"cyd","permission":"status:read","at":"2024-05-01T12:00:00Z","ip":"1.1.1.1"}';
  assertEqual(authorize(POLICY, request), '{"decision":"allow","reason":"allowed by rule 7","roles":["viewer"]}');
}

export function testRoleCycle(): void {
  const request = '{"userId":"dan","permission":"docs:read","at":"2024-05-01T12:00:00Z","ip":"1.1.1.1"}';
  assertEqual(authorize(POLICY, request), '{"decision":"allow","reason":"allowed by rule 1","roles":["loop","loop2","viewer"]}');
}

export function testUnknownUser(): void {
  const request = '{"userId":"zed","permission":"status:read","at":"2024-05-01T12:00:00Z","ip":"1.1.1.1"}';
  assertEqual(authorize(POLICY, request), '{"decision":"deny","reason":"unknown user \\"zed\\"","roles":[]}');
}

export function testInvalidTime(): void {
  assertEqual(authorize(POLICY, '{"userId":"eve","permission":"status:read","at":"yesterday","ip":"1.1.1.1"}'), '{"decision":"deny","reason":"invalid time \\"yesterday\\"","roles":[]}');
}

export function testInvalidJson(): void {
  assertEqual(authorize(POLICY, '{"userId":"ann"'), '{"decision":"deny","reason":"invalid json","roles":[]}');
}
