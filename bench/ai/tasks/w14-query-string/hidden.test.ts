import { assertEqual, assertThrows } from "e7:test";
import { parseQuery, buildQuery, getParam, setParam } from "./solution.ts";

function kv(key: string, value: string): { key: string; value: string } {
  return { key, value };
}

export function testParseBasics(): void {
  assertEqual(parseQuery(""), []);
  assertEqual(parseQuery("?"), []);
  assertEqual(parseQuery("a=1"), [kv("a", "1")]);
  assertEqual(parseQuery("&&a=1&&b=2&"), [kv("a", "1"), kv("b", "2")]);
  assertEqual(parseQuery("flag&x="), [kv("flag", ""), kv("x", "")]);
  assertEqual(parseQuery("=v"), [kv("", "v")]);
  assertEqual(parseQuery("eq=a=b==c"), [kv("eq", "a=b==c")]);
  assertEqual(parseQuery("??a=1"), [kv("?a", "1")]);
}

export function testParseDecoding(): void {
  assertEqual(parseQuery("a+b=c+d"), [kv("a b", "c d")]);
  assertEqual(parseQuery("plus=%2B&sp=%20"), [kv("plus", "+"), kv("sp", " ")]);
  assertEqual(parseQuery("e=%e2%82%ac&rocket=%F0%9F%9A%80"), [kv("e", "€"), kv("rocket", "🚀")]);
  assertEqual(parseQuery("amp=%26&eq=%3D&hash=%23"), [kv("amp", "&"), kv("eq", "="), kv("hash", "#")]);
  assertEqual(parseQuery("raw=ü"), [kv("raw", "ü")]);
}

export function testParseErrors(): void {
  assertThrows(() => { parseQuery("ok=1&q=100%"); }, "invalid encoding: q=100%");
  assertThrows(() => { parseQuery("q=%zz"); }, "invalid encoding: q=%zz");
  assertThrows(() => { parseQuery("%4=1"); }, "invalid encoding: %4=1");
  assertThrows(() => { parseQuery("bad=%C3"); }, "invalid encoding: bad=%C3");
  assertThrows(() => { parseQuery("bad=%FF%FE"); }, "invalid encoding: bad=%FF%FE");
}

export function testBuildEncoding(): void {
  assertEqual(buildQuery([]), "");
  assertEqual(buildQuery([kv("safe", "AZaz09-._~")]), "safe=AZaz09-._~");
  assertEqual(buildQuery([kv("sub", "!'()*")]), "sub=%21%27%28%29%2A");
  assertEqual(buildQuery([kv("a b", "c+d")]), "a+b=c%2Bd");
  assertEqual(buildQuery([kv("x", "&=?#/%")]), "x=%26%3D%3F%23%2F%25");
  assertEqual(buildQuery([kv("u", "é€🚀")]), "u=%C3%A9%E2%82%AC%F0%9F%9A%80");
  assertEqual(buildQuery([kv("empty", ""), kv("", "v")]), "empty=&=v");
  assertEqual(buildQuery([kv("nl", "a\nb\tc")]), "nl=a%0Ab%09c");
}

export function testRoundTrip(): void {
  const params = [kv("q", "1 + 1 = 2 & more"), kv("name", "Zoë O'Brien"), kv("q", "dup"), kv("emoji", "👍🏽")];
  assertEqual(parseQuery(buildQuery(params)), params);
  assertEqual(parseQuery("?" + buildQuery(params)), params);
}

export function testGetParam(): void {
  const q = "?a=1&b=two+words&a=3&c";
  assertEqual(getParam(q, "a"), "1");
  assertEqual(getParam(q, "b"), "two words");
  assertEqual(getParam(q, "c"), "");
  assertEqual(getParam(q, "d"), null);
  assertEqual(getParam(q, "A"), null);
  assertEqual(getParam("caf%C3%A9=yes", "café"), "yes");
}

export function testSetParamReplaceAndDedupe(): void {
  assertEqual(setParam("https://x.io/p?a=1&b=2&a=3#top", "a", "9"), "https://x.io/p?a=9&b=2#top");
  assertEqual(setParam("https://x.io/p?b=2&a=1&a=3", "a", "new value"), "https://x.io/p?b=2&a=new+value");
}

export function testSetParamAppend(): void {
  assertEqual(setParam("https://x.io/p", "a", "1"), "https://x.io/p?a=1");
  assertEqual(setParam("https://x.io/p?", "a", "1"), "https://x.io/p?a=1");
  assertEqual(setParam("https://x.io/p?b=2", "a", "1"), "https://x.io/p?b=2&a=1");
  assertEqual(setParam("https://x.io/p#frag", "a", "1"), "https://x.io/p?a=1#frag");
  assertEqual(setParam("/relative?x=1", "y", "é"), "/relative?x=1&y=%C3%A9");
}

export function testSetParamNormalizesAndKeepsFragment(): void {
  assertEqual(setParam("app://open?msg=hi%20there&n=1", "n", "2"), "app://open?msg=hi+there&n=2");
  // a ? or & inside the fragment is not part of the query
  assertEqual(setParam("https://x.io/?a=1#/route?b=2&c=3", "b", "5"), "https://x.io/?a=1&b=5#/route?b=2&c=3");
  assertEqual(setParam("https://x.io/#x?y", "k", "v"), "https://x.io/?k=v#x?y");
}

export function testSetParamEncodesKey(): void {
  assertEqual(setParam("https://x.io/?a=1", "my key", "a&b"), "https://x.io/?a=1&my+key=a%26b");
  assertEqual(setParam("https://x.io/?my+key=1", "my key", "2"), "https://x.io/?my+key=2");
}
