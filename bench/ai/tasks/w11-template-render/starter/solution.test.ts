import { assertEqual, assertThrows } from "e7:test";
import { renderTemplate } from "./solution.ts";

export function testEscaping(): void {
  const vars = new Map<string, string>([["name", "Tom & <Jerry>"]]);
  assertEqual(renderTemplate("Hi {{ name }}!", vars), "Hi Tom &amp; &lt;Jerry&gt;!");
  assertEqual(renderTemplate("Hi {{{name}}}!", vars), "Hi Tom & <Jerry>!");
}

export function testFilters(): void {
  const vars = new Map<string, string>([["city", "oslo"]]);
  assertEqual(renderTemplate("{{ city | upper }} / {{ country | default: n/a }}", vars), "OSLO / n/a");
}

export function testMissing(): void {
  assertThrows(() => { renderTemplate("Hello {{who}}", new Map<string, string>()); }, 'missing variable "who"');
}
