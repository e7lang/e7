// Mismatches with Node.js found by the differential fuzzer (tests/difffuzz); output is compared with Node.js

// Map.set / Set.add store a -0 key as +0
const m = new Map<number, string>();
m.set(-0, "neg");
for (const [k, v] of m) console.log(k, v, 1 / k, m.has(0), m.get(0));
const s = new Set<number>([-0]);
s.add(-0);
for (const x of s) console.log(x, 1 / x, s.size);
console.log(JSON.stringify([...new Map<number, number>([[-0, 1], [0, 2]]).keys()].map((k: number): number => 1 / k)));

// (-0).toFixed(n) has no sign; tiny negative values keep it
const negZero: number = -0;
console.log(negZero.toFixed(2), (-0.0001).toFixed(2), negZero.toFixed(0), (-0.5).toFixed(0));

// toString(radix): V8's digits (integer digits beyond 2^53 are zeros, fractions only to the input's precision)
const vals: number[] = [1e21, 1.7976931348623157e308, 9007199254740993, 2 ** 60 + 2 ** 10, 12345678901234567890, 0.1, 1 / 3,
  123.456, -255.875, 35.99999999, 5e-324, 4294967295.5, 0.999999999999];
for (const v of vals) console.log(v.toString(2), v.toString(3), v.toString(7), v.toString(16), v.toString(36));

// shortest round-trip digits when the closest shortest decimal is not the printf-rounded one (powers of two)
console.log(2 ** 122, -(2 ** 122), Number(-(2n ** 122n)), String(2 ** 122), `${2 ** 122}`, JSON.stringify([2 ** 122]));
let pw = "";
for (let e = -1074; e <= 1023; e += 7) pw += String(2 ** e).length + ",";
console.log(pw);

// parseInt beyond 2^53: V8's algorithms (powers of two rounded half to even, other radices in 32-bit chunks
// after the leading zeros)
console.log(parseInt("9223372036854775807", 36), parseInt("0g848c2cg2ce260866g6608ece68c68ge844e66ca8242e260224geeg24", 18),
  parseInt("1fffffffffffff9", 16), parseInt("1fffffffffffff8", 16), parseInt("7777777777777777777777", 8),
  parseInt("011882663408622405305007445273230873531330145714214243044214", 9), parseInt("-0", 16));

// x ** 0.5 is sqrt(x) (as V8's fdlibm pow; the C library's pow can be 1 ulp off)
console.log(Number.MAX_VALUE ** 0.5, Math.pow(Number.MAX_VALUE, 0.5), (-0) ** 0.5, (-Infinity) ** 0.5, 2 ** 0.5);

// getDay() of a Sunday before 1970 is +0
console.log(new Date(69, -13, 17, 6, -47, -12, 2552).getDay() * -1, new Date(-86400000 * 4).getUTCDay() * -1);

// Date strings for years outside 0..9999
const years: number[] = [-271821, -12345, -1, 10000, 275760];
for (const y of years) {
  const d = new Date(Date.UTC(2000, 0, 1));
  d.setTime(Date.UTC(y, y === 275760 ? 8 : 3, y === -271821 ? 20 : 13, 1, 2, 3, 4));
  console.log(d.toString());
  console.log(d.toUTCString(), "|", d.toDateString(), "|", d.toTimeString());
}

// encodeURI / encodeURIComponent encode NUL
console.log(JSON.stringify(encodeURI("\u0000a b")), encodeURIComponent("\u0000x"), encodeURI("\u0000").length);

// Date.parse of RFC 1123 / toString formats like V8: no signed years, two-digit years, field ranges
const texts: string[] = ["Fri, 01 Jan -0001 00:00:00 GMT", "Fri, 01 Jan +2001 00:00:00 GMT", "Fri, 01 Jan 99 00:00:00 GMT",
  "Fri, 01 Jan 49 00:00:00 GMT", "Fri, 32 Jan 2001 00:00:00 GMT", "Fri, 01 Jan 2001 00:60:00 GMT",
  "Fri, 01 Jan 2001 24:00:00 GMT", "Thu, 13 Apr 10000 01:02:03 GMT", "Tue Nov 14 2023 22:13:20 GMT-0500"];
for (const t of texts) console.log(t, Date.parse(t));
