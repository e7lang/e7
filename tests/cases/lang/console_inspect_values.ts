// console.log formats values like Node.js util.inspect: primitives, strings (quoting and escaping), keys.

// numbers
console.log(0, -0, 1.5, -2, NaN, Infinity, -Infinity);
console.log(1e21, 1e-7, 123456789012345680000, 5e-324, 1.7976931348623157e308, 0.1 + 0.2, 2 ** 53);
console.log([0, -0, NaN, -Infinity, 1e21, 1e-7]);
const ints: i32[] = [i32(-5), i32(7)];
console.log(i32(-3), u8(200), ints);
console.log(true, false, [true, false]);
console.log(10n, -123456789012345678901234567890n, [0n, -1n]);
let maybe: number | null = null;
let missing: string | undefined = undefined;
console.log(maybe, missing, [maybe, 4], [missing, "x"]);
maybe = 3;
console.log(maybe, [maybe]);

// top-level strings print as they are, nested ones quoted
console.log("plain 'text' with \"quotes\"\tand\nnewline");
console.log(["plain", "it's", "say \"hi\"", "both ' and \"", "all ' \" `", "all ' \" ${x}"]);
console.log(["tab\t", "nl\n", "cr\r", "bs\\", "nul\0", "bell\x07", "esc\x1b", "del\x7f", "c1\x85\x9f", "nbsp\xa0"]);
console.log(["\b\f\v", "\u0001\u001f", "é中😀", "\ud800", "x\udc00y", "\ud83d\ude00", "\u200b\u2028\ufeff"]);
console.log({ s: "", t: "'", u: "\"", v: "`", w: "'\"", x: "'`", y: "\"`", z: "'\"`" });

// property names: identifiers bare (no '$'), everything else quoted
const keys = { a: 1, _b: 2, $c: 3, "d-e": 4, "f g": 5, "it's": 6, "ключ": 7, "中": 8, "k\n": 9, A1: 10, "1a": 11 };
console.log(keys);

// integer-like keys come first in ascending order, as in JavaScript objects
console.log({ z: 1, "10": 2, "2": 3, "0": 4, "01": 5, "4294967294": 6, "4294967295": 7, a: 8 });

// optional properties that are absent are not shown
interface Opt { name: string; note?: string; count?: number; tags?: string[] }
const opts: Opt[] = [{ name: "a" }, { name: "b", count: 2 }, { tags: [], name: "c" }];
console.log(opts);
const empty: { a?: number } = {};
console.log(empty, [empty]);
