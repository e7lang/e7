import { assertEqual } from "e7:test";
export function testUpper(): void { assertEqual("abc".toUpperCase(), "ABC"); }
export function testSplit(): void { assertEqual("a,b".split(","), ["a", "b"]); }
