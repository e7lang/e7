// narrowing that tsc does and E7 follows: declaration initializers, the ends of blocks and branches,
// loop conditions, optional chains, string literals, predicates of arrow functions
class N1 { next: N1 | null = null; constructor(public v: number) {} }
let s: string | null = "abc";
console.log(s.length);
let cur: N1 | null = new N1(1);
cur.next = new N1(2);
console.log(cur.v, cur.next.v);

function doWhile(): number {
  let n: number | undefined;
  do { n = 5; } while (false);
  return n;
}
const m = new Map<string, N1>();
function nested(k: string): number {
  let x = m.get(k);
  if (x === undefined) {
    if (k.length > 1) x = new N1(2);
    else x = new N1(3);
  }
  return x.v;
}
console.log(doWhile(), nested("a"), nested("ab"));

const st = [1, 2, 3];
let top: number | undefined;
while ((top = st.pop()) !== undefined) console.log(top * 2);
let q: N1 | null = null;
while (q === null) q = new N1(9);
console.log(q.v);

function opt(o: { v?: number } | null): number {
  if (o?.v !== undefined) return o.v;
  return -1;
}
function chain(a: { b?: { c: number[] | null } }): number {
  if (a.b?.c) return a.b.c.length;
  return -2;
}
function eq(u: { name: string } | undefined): string {
  if (u?.name === "x") return u.name.toUpperCase();
  return "-";
}
console.log(opt({ v: 3 }), opt(null), opt({}), chain({ b: { c: [1] } }), chain({}), eq({ name: "x" }), eq(undefined));

class Animal { constructor(public name: string) {} }
class Dog extends Animal {}
function speak(a: Animal | null): string {
  if (a instanceof Dog) return "dog";
  else if (a === null) return "null";
  return a.name;
}
console.log(speak(new Animal("cat")), speak(new Dog("d")), speak(null));

function three(n: 1 | 2 | 3 | null | undefined): number {
  switch (n) {
    case null: return -1;
    case undefined: return -2;
    case 1: return 1;
    default: return n + 1;
  }
}
function strsw(t: string | null): number {
  switch (t) {
    case null: return -1;
    default: return t.length;
  }
}
console.log(three(2), three(null), three(undefined), strsw("abc"), strsw(null));

type Kind = "a" | "b";
const isK = (k: string): k is Kind => k === "a" || k === "b";
function useK(k: Kind): string { return "K" + k; }
const r: string = "a";
if (isK(r)) console.log(useK(r));
const t2: string = "b";
if (t2 === "a" || t2 === "b") console.log(useK(t2));
function sw(k: string): string { switch (k) { case "a": case "b": return useK(k); default: return "-"; } }
console.log(sw("a"), sw("z"));

const xs: (string | null)[] = ["a", null, "b"];
const ys = xs.filter((x): x is string => x !== null);
console.log(ys.map((y) => y.toUpperCase()), xs.find((x): x is string => x !== null)?.length);
class D2 extends Animal { bark(): string { return this.name + "!"; } }
const as: Animal[] = [new D2("rex"), new Animal("tom")];
console.log(as.filter((z): z is D2 => z instanceof D2).map((d) => d.bark()));
