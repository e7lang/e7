interface Opt { a: number; b?: string | undefined; c: number | undefined; d?: number }
const o1: Opt = { a: 1, c: undefined };
const o2: Opt = { a: 2, b: undefined, c: 3 };
const o3: Opt = { a: 3, b: "x", c: undefined, d: 4 };
console.log(o1, o2, o3);
console.log(Object.keys(o1), Object.keys(o2), Object.keys(o3));
console.log(JSON.stringify(o1), JSON.stringify(o2));
const s1 = { ...o1 };
const s2: Opt = { ...o3, ...o2 };
const s3: Opt = { ...o2, ...o1 };
console.log(s1, s2, s3);
o1.b = undefined;
console.log(Object.keys(o1));
const p: Opt = JSON.parse('{"a":5,"c":6}') as Opt;
console.log(p, Object.keys(p));
function* g(): Generator<number> { yield 1; }
const it = g();
console.log(it.next(), it.next());
