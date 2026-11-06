function* fib(): Generator<number> {
  let a = 0, b = 1;
  while (true) {
    yield a;
    [a, b] = [b, a + b];
  }
}
function* take<T>(it: Iterator<T>, n: number): Generator<T> {
  for (let i = 0; i < n; i++) {
    const r = it.next();
    if (r.done) return;
    yield r.value;
  }
}
console.log([...take(fib(), 10)]);
console.log(Array.from(take(fib(), 5)));
const [x, y, ...rest] = take(fib(), 6);
console.log(x, y, rest);

const words = function* (s: string) {
  for (const w of s.split(" ")) yield w.toUpperCase();
};
for (const w of words("hello generator world")) console.log(w);

function* pairs(m: Map<string, number>): Generator<[string, number]> {
  for (const [k, v] of m) yield [k, v * 2];
}
for (const [k, v] of pairs(new Map([["a", 1], ["b", 2]]))) console.log(k, v);

function makeCounter() {
  let calls = 0;
  function* gen() {
    while (calls < 3) yield ++calls;
  }
  return gen;
}
const mk = makeCounter();
console.log([...mk()], [...mk()]);

class Tree {
  children: Tree[] = [];
  constructor(public label: string) {}
  add(t: Tree): Tree { this.children.push(t); return this; }
  *walk(depth = 0): Generator<string> {
    yield "  ".repeat(depth) + this.label;
    for (const c of this.children) yield* c.walk(depth + 1);
  }
}
const tree = new Tree("root").add(new Tree("a").add(new Tree("a1"))).add(new Tree("b"));
for (const line of tree.walk()) console.log(line);
const gen = fib();
console.log(typeof gen);
let total = 0;
for (const v of fib()) { if (v > 100) break; total += v; }
console.log(total);
function* early(): Generator<number> {
  try { yield 1; yield 2; } finally { console.log("closed"); }
}
function first(): number {
  for (const v of early()) return v;
  return -1;
}
console.log(first());
try {
  for (const v of early()) { throw new Error("stop " + v); }
} catch (e) {
  if (e instanceof Error) console.log(e.message);
}
