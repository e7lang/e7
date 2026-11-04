// labeled block, static members, ??=, instanceof narrowing, spreads, Object.keys, typeof, getters-free classes
blk: {
  if (Math.random() >= 0) break blk;
  console.log("unreachable");
}
class Registry {
  static items: string[] = [];
  static add(s: string): number { Registry.items.push(s); return Registry.items.length; }
}
Registry.add("a"); Registry.add("b");
console.log(Registry.items.join(","), Registry.add("c"));
let cache: Map<string, number> | undefined;
cache ??= new Map<string, number>();
cache.set("x", 1);
console.log(cache.size);
class Animal2 { constructor(public name: string) {} }
class Cat extends Animal2 { meow(): string { return this.name + " meows"; } }
function speak(a: Animal2): string {
  if (a instanceof Cat) return a.meow();
  return a.name + " is quiet";
}
console.log(speak(new Cat("Tom")), speak(new Animal2("Rock")));
const set = new Set<number>([3, 1, 2]);
const arr = [...set, ...[9, 8]];
console.log(arr.join(","), Array.from(set).length, [..."héllo"].length);
const m = new Map<string, number>([["a", 1], ["b", 2]]);
console.log(JSON.stringify([...m]), JSON.stringify(Array.from(m.entries())), [...m.values()].join("+"));
const rec = { alpha: 1, beta: "two", gamma: true };
console.log(Object.keys(rec).join(","), typeof rec.alpha, typeof rec.beta, typeof rec.gamma);
enum Mode { Fast = "fast", Safe = "safe" }
function pick(s: string): Mode { return s === "f" ? Mode.Fast : Mode.Safe; }
switch (pick("f")) {
  case Mode.Fast: console.log("fast mode"); break;
  case Mode.Safe: console.log("safe mode"); break;
}
const nested = { a: { b: { c: [1, 2, 3] } } };
console.log(nested.a.b.c[2], nested?.a?.b?.c.length);
let n: number | null = null;
const v = n ?? 5;
n = 7;
console.log(v, n + 1);
const s1 = `multi
line ${"template"} \${not} ${`nested ${1 + 1}`}`;
console.log(s1);
const big = [1e21, 1.5e-7, 123e-20, -1e-7, 2 ** 70];
console.log(big.map(x => String(x)).join(" "));
console.log((0.1).toFixed(20), (1e-10).toPrecision(3), (255).toString(36), (-255.5).toString(16));
const xs = [5, 1, 4];
const [first, second] = [xs[0], xs[1]];
console.log(first, second);
