import { assertEqual, assertThrows } from "e7:test";
import { parseCsv, toCsv, parseCsvObjects } from "./solution.ts";

export function testSimple(): void {
  assertEqual(parseCsv("a,b,c"), [["a", "b", "c"]]);
  assertEqual(parseCsv("a,b\nc,d"), [["a", "b"], ["c", "d"]]);
  assertEqual(parseCsv("a,b\r\nc,d\r\n"), [["a", "b"], ["c", "d"]]);
}

export function testEmptyThings(): void {
  assertEqual(parseCsv(""), []);
  assertEqual(parseCsv("\n"), [[""]]);
  assertEqual(parseCsv(","), [["", ""]]);
  assertEqual(parseCsv("a,"), [["a", ""]]);
  assertEqual(parseCsv(",a"), [["", "a"]]);
  assertEqual(parseCsv("a\n\nb"), [["a"], [""], ["b"]]);
  assertEqual(parseCsv("a\n\n"), [["a"], [""]]);
  assertEqual(parseCsv('""'), [[""]]);
  assertEqual(parseCsv('"",""\n'), [["", ""]]);
}

export function testQuotedFields(): void {
  assertEqual(parseCsv('"a,b","c""d"'), [["a,b", 'c"d']]);
  assertEqual(parseCsv('"line1\nline2",x\ny,z'), [["line1\nline2", "x"], ["y", "z"]]);
  assertEqual(parseCsv('"crlf\r\ninside"'), [["crlf\r\ninside"]]);
  assertEqual(parseCsv('""""'), [['"']]);
  assertEqual(parseCsv('"a""",b'), [['a"', "b"]]);
}

export function testQuoteInsideUnquoted(): void {
  assertEqual(parseCsv('5" disk,ab"c"'), [['5" disk', 'ab"c"']]);
  // a field only counts as quoted when the quote is its first character
  assertEqual(parseCsv(' "x",y'), [[' "x"', "y"]]);
}

export function testNoTrimming(): void {
  assertEqual(parseCsv(" a , b \n"), [[" a ", " b "]]);
  assertEqual(parseCsv("a\tb,c"), [["a\tb", "c"]]);
}

export function testLoneCarriageReturn(): void {
  assertEqual(parseCsv("a\rb,c"), [["a\rb", "c"]]);
  assertEqual(parseCsv("a\r"), [["a\r"]]);
}

export function testUnicode(): void {
  assertEqual(parseCsv('Zoë,"🚀, launch"\n'), [["Zoë", "🚀, launch"]]);
}

export function testUnterminatedQuote(): void {
  assertThrows(() => { parseCsv('"abc'); }, "unterminated quote starting on line 1");
  assertThrows(() => { parseCsv('a,b\nc,"d\ne\nf'); }, "unterminated quote starting on line 2");
  assertThrows(() => { parseCsv('x\n"a""'); }, "unterminated quote starting on line 2");
}

export function testCharacterAfterQuote(): void {
  assertThrows(() => { parseCsv('"a"b,c'); }, "unexpected character after quote on line 1");
  assertThrows(() => { parseCsv('x\n"a" ,c'); }, "unexpected character after quote on line 2");
  assertThrows(() => { parseCsv('"multi\nline"x'); }, "unexpected character after quote on line 2");
  assertThrows(() => { parseCsv('"a"\rb'); }, "unexpected character after quote on line 1");
}

export function testToCsvQuoting(): void {
  assertEqual(toCsv([["plain", "with,comma", 'with"quote', "with\nnewline", "with\rcr"]]),
    'plain,"with,comma","with""quote","with\nnewline","with\rcr"');
  assertEqual(toCsv([[" lead", "trail ", "in side", ""]]), '" lead","trail ",in side,');
  assertEqual(toCsv([["a"], ["b", "c"]]), "a\r\nb,c");
  assertEqual(toCsv([]), "");
}

export function testRoundTrip(): void {
  const rows = [["id", "text"], ["1", 'He said "no", then left.\nNext line'], ["2", " padded "], ["3", ""]];
  assertEqual(parseCsv(toCsv(rows)), rows);
}

export function testObjects(): void {
  const text = 'name,email,city\r\n"Lee, Ann",ann@x.io,Oslo\r\nBo,bo@x.io,""\r\n';
  const objs = parseCsvObjects(text);
  assertEqual(objs.length, 2);
  assertEqual([...objs[0].keys()], ["name", "email", "city"]);
  assertEqual(objs[0].get("name"), "Lee, Ann");
  assertEqual(objs[1], new Map<string, string>([["name", "Bo"], ["email", "bo@x.io"], ["city", ""]]));
}

export function testObjectsEdgeCases(): void {
  assertEqual(parseCsvObjects(""), []);
  assertEqual(parseCsvObjects("a,b\n"), []);
  assertThrows(() => { parseCsvObjects("a,b\n1,2\n3\n"); }, "record 3 has 1 fields, expected 2");
  assertThrows(() => { parseCsvObjects("a,b\n1,2,3"); }, "record 2 has 3 fields, expected 2");
}
