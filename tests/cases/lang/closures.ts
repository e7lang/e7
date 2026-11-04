function makeAdders(): ((x: number) => number)[] {
  const out: ((x: number) => number)[] = [];
  for (const k of [1, 10, 100]) out.push(x => x + k);
  return out;
}
console.log(makeAdders().map(f => f(5)).join(","));
function counterPair(): [() => number, () => number] {
  let n = 0;
  return [() => ++n, () => --n];
}
const [inc, dec] = counterPair();
inc(); inc(); inc(); dec();
console.log(inc());
function outer(a: number): () => () => number {
  let b = a * 2;
  return () => {
    let c = b + 1;
    b++;
    return () => a + b + c++;
  };
}
const mid = outer(5);
const f1 = mid(), f2 = mid();
console.log(f1(), f1(), f2(), f2());
class Timer {
  ticks = 0;
  handlers: (() => void)[] = [];
  onTick(): () => number { return () => ++this.ticks; }
  register(): void { this.handlers.push(() => { this.ticks += 10; }); }
}
const t = new Timer();
const tick = t.onTick();
tick(); tick();
t.register(); t.register();
for (const h of t.handlers) h();
console.log(t.ticks);
const fns: (() => string)[] = [];
for (let i = 0; i < 4; i++) {
  if (i === 2) continue;
  const label = "f" + i;
  fns.push(() => label + ":" + i);
}
console.log(fns.map(f => f()).join(" "));
let shared = 0;
function bumpLater(): () => void { return () => { shared += 5; }; }
bumpLater()();
bumpLater()();
console.log(shared);
function recurseInner(n: number): number {
  function go(k: number): number { return k <= 0 ? 0 : k + go(k - 1); }
  return go(n);
}
console.log(recurseInner(100));
function memoize(f: (n: number) => number): (n: number) => number {
  const cache = new Map<number, number>();
  return (n) => {
    const hit = cache.get(n);
    if (hit !== undefined) return hit;
    const v = f(n);
    cache.set(n, v);
    return v;
  };
}
let evals = 0;
const slowSq = memoize((n) => { evals++; return n * n; });
console.log(slowSq(4), slowSq(4), slowSq(5), evals);
const compose2 = (...fs: ((x: number) => number)[]) => (x: number) => fs.reduceRight((acc, f) => f(acc), x);
console.log(compose2(x => x + 1, x => x * 2)(5));
function withDefault(a: number, b: number = a * 2, c: string = `${a}-${b}`): string { return c; }
console.log(withDefault(1), withDefault(1, 5), withDefault(1, 5, "z"));
let x = 1;
const getX = () => x;
x = 42;
console.log(getX());
