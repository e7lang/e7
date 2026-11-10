import { assertEqual } from "e7:test";
import { authorize } from "./solution.ts";

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

export function testViewerCanRead(): void {
  const request = '{"userId":"cyd","permission":"docs:read","at":"2024-05-01T12:00:00Z","ip":"1.2.3.4"}';
  assertEqual(authorize(POLICY, request), '{"decision":"allow","reason":"allowed by rule 1","roles":["viewer"]}');
}

export function testNoMatchingRule(): void {
  const request = '{"userId":"bob","permission":"billing.invoices:read","at":"2024-05-01T12:00:00Z","ip":"10.0.0.7"}';
  assertEqual(authorize(POLICY, request), '{"decision":"deny","reason":"no matching rule","roles":["editor","viewer"]}');
}
