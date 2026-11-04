interface Cfg { host: string; port: number; tls?: boolean; tags: string[] }
const base: Cfg = { host: "localhost", port: 80, tags: ["a"] };
const prod: Cfg = { ...base, host: "example.com", tls: true };
console.log(JSON.stringify(base), JSON.stringify(prod));
const merged = { ...base, port: 8443, extra: 1 };
console.log(JSON.stringify(merged), merged.extra + merged.port);
class P { constructor(public x: number, public y: number) {} }
const p = new P(1, 2);
const q = { ...p, z: 3 };
console.log(JSON.stringify(q), q.x + q.y + q.z);
const ab: { a: number; b?: number } = { a: 2, b: 3 };
const order = { b: 1, ...ab, c: 4 };
console.log(JSON.stringify(order));
let n = 0;
const side = { ...{ v: ++n }, w: ++n, ...{ v: ++n } };
console.log(JSON.stringify(side));
function withDefaults(o: { name: string; retries?: number }): { name: string; retries: number } {
  return { retries: 3, ...o, name: o.name.toUpperCase() };
}
console.log(JSON.stringify(withDefaults({ name: "x" })));
console.log(JSON.stringify(withDefaults({ name: "y", retries: 0 })));
interface Opts { a?: number; b?: string; c?: boolean }
function fill(o: Opts, p: Opts): { a: number; b: string; c: boolean } {
  return { a: 1, b: "def", c: false, ...o, ...p };
}
console.log(JSON.stringify(fill({}, {})), JSON.stringify(fill({ a: 5 }, { a: 7, c: true })), JSON.stringify(fill({ b: "o" }, { a: 2 })));
const both: Opts = { ...{ a: 1 } as Opts, ...{ b: "x" } as Opts };
console.log(JSON.stringify(both), both.a, both.b, both.c);
class Box { v = 1; get2(): number { return this.v * 2; } }
const bx = { ...new Box(), w: 2 };
console.log(JSON.stringify(bx), bx.v + bx.w);
const nested = { inner: { ...base }, list: [...base.tags, "b"] };
nested.inner.tags.push("z");
console.log(JSON.stringify(nested), base.tags.length);
