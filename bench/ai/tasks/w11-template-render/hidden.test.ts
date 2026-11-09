import { assertEqual, assertThrows } from "e7:test";
import { renderTemplate } from "./solution.ts";

function vars(...pairs: string[]): Map<string, string> {
  const m = new Map<string, string>();
  for (let i = 0; i + 1 < pairs.length; i += 2) m.set(pairs[i], pairs[i + 1]);
  return m;
}

function errorOf(t: string, v: Map<string, string>): string {
  try {
    renderTemplate(t, v);
  } catch (e) {
    if (e instanceof Error) return e.message;
  }
  return "no error";
}

export function testPlainText(): void {
  assertEqual(renderTemplate("", vars()), "");
  assertEqual(renderTemplate("no tags at all", vars()), "no tags at all");
  assertEqual(renderTemplate("stray }} and } and {", vars()), "stray }} and } and {");
}

export function testAllEscapes(): void {
  const v = vars("x", `<a href="/q?a=1&b='2'">`);
  assertEqual(renderTemplate("{{x}}", v), "&lt;a href=&quot;/q?a=1&amp;b=&#39;2&#39;&quot;&gt;");
  assertEqual(renderTemplate("{{{x}}}", v), `<a href="/q?a=1&b='2'">`);
  // no double escaping of existing entities
  assertEqual(renderTemplate("{{x}}", vars("x", "&amp;")), "&amp;amp;");
}

export function testSpacingAndNames(): void {
  const v = vars("user.first_name", "Ada", "n2", "7");
  assertEqual(renderTemplate("{{user.first_name}}|{{   user.first_name   }}|{{{ n2 }}}", v), "Ada|Ada|7");
  assertEqual(renderTemplate("{{n2}}{{n2}}", v), "77");
  assertEqual(renderTemplate("a{{ n2 }}b{{n2}}}c", v), "a7b7}c");
}

export function testValuesAreNotReparsed(): void {
  assertEqual(renderTemplate("{{a}}", vars("a", "{{b}}")), "{{b}}");
}

export function testUnicodeValues(): void {
  assertEqual(renderTemplate("¡Hola {{name | upper}}!", vars("name", "zoë")), "¡Hola ZOË!");
}

export function testFilterChain(): void {
  const v = vars("s", "  MiXed Case  ");
  assertEqual(renderTemplate("[{{ s | trim }}]", v), "[MiXed Case]");
  assertEqual(renderTemplate("[{{ s | lower | trim }}]", v), "[mixed case]");
  assertEqual(renderTemplate("[{{ s|upper|trim }}]", v), "[MIXED CASE]");
  assertEqual(renderTemplate("[{{{ s | trim }}}]", v), "[MiXed Case]");
}

export function testDefault(): void {
  const v = vars("empty", "", "set", "yes");
  assertEqual(renderTemplate("{{ missing | default:none }}", v), "none");
  assertEqual(renderTemplate("{{ empty | default: (blank) }}", v), "(blank)");
  assertEqual(renderTemplate("{{ set | default:none }}", v), "yes");
  assertEqual(renderTemplate("[{{ empty }}]", v), "[]");
  // other filters apply to the fallback, in order, and the result is escaped
  assertEqual(renderTemplate("{{ missing | default: guest | upper }}", v), "GUEST");
  assertEqual(renderTemplate("{{ missing | upper | default: guest }}", v), "GUEST");
  assertEqual(renderTemplate("{{ missing | default:<none> }}", v), "&lt;none&gt;");
  assertEqual(renderTemplate("{{ missing | default: }}", v), "");
}

export function testUnclosed(): void {
  assertEqual(errorOf("Hello {{ name", vars("name", "x")), "unclosed tag at 6");
  assertEqual(errorOf("ab {{{ name }}", vars("name", "x")), "unclosed tag at 3");
  assertEqual(errorOf("{{a}} {{", vars("a", "1")), "unclosed tag at 6");
}

export function testInvalidTag(): void {
  assertEqual(errorOf("x{{}}", vars()), "invalid tag at 1");
  assertEqual(errorOf("x {{   }}", vars()), "invalid tag at 2");
  assertEqual(errorOf("{{ first name }}", vars()), "invalid tag at 0");
  assertEqual(errorOf("ok {{ a-b }}", vars()), "invalid tag at 3");
  assertEqual(errorOf("{{ | upper }}", vars()), "invalid tag at 0");
}

export function testUnknownFilter(): void {
  assertEqual(errorOf("{{ a | shout }}", vars("a", "x")), 'unknown filter "shout"');
  assertEqual(errorOf("{{ a | upper | Trim }}", vars("a", "x")), 'unknown filter "Trim"');
  assertEqual(errorOf("{{ a | }}", vars("a", "x")), 'unknown filter ""');
}

export function testErrorPrecedence(): void {
  // within a tag: name, then filter, then the variable
  assertEqual(errorOf("{{ missing | shout }}", vars()), 'unknown filter "shout"');
  assertEqual(errorOf("{{ bad! | shout }}", vars()), "invalid tag at 0");
  // across tags: the first one in the text wins
  assertEqual(errorOf("{{ missing }} {{ x | shout }}", vars("x", "1")), 'missing variable "missing"');
  assertEqual(errorOf("{{ x | shout }} {{ missing }}", vars("x", "1")), 'unknown filter "shout"');
  assertEqual(errorOf("{{ missing }} {{ unclosed", vars()), 'missing variable "missing"');
}
