// Generics (monomorphization): generic functions, arrow functions, classes, interfaces, aliases,
// inference from arguments and contextual types, nested generic functions capturing locals.
function first<T>(xs: T[]): T {
  return xs[0];
}
console.log(first([1, 2]), first(["a", "b"]), first<boolean>([true]));

function map<T, U>(xs: T[], f: (x: T, i: number) => U): U[] {
  const out: U[] = [];
  for (let i = 0; i < xs.length; i++) out.push(f(xs[i], i));
  return out;
}
console.log(map([1, 2, 3], (x) => x * 2), map(["a"], (s, i) => s + i));

const identity = <T,>(x: T): T => x;
console.log(identity(5), identity("s"));

class Box<T> {
  constructor(public value: T) {}
  get(): T {
    return this.value;
  }
  map<U>(f: (v: T) => U): Box<U> {
    return new Box(f(this.value));
  }
  static count = 0;
  static make(): number {
    return ++Box.count;
  }
}
const b = new Box(3);
console.log(b.get(), b.map((v) => "x" + v).get(), b, Box.make(), Box.make());
const bs: Box<string> = new Box("hi");
console.log(bs.value.length);

interface Pair<A, B> {
  first: A;
  second: B;
}
function swap<A, B>(p: Pair<A, B>): Pair<B, A> {
  return { first: p.second, second: p.first };
}
console.log(swap({ first: 1, second: "one" }));

type Result<T> = { ok: boolean; value: T | null };
function ok<T>(v: T): Result<T> {
  return { ok: true, value: v };
}
console.log(ok(1), ok("s").value);

function outer(k: number): number[] {
  function addK<T>(xs: T[], f: (x: T) => number): number[] {
    return xs.map((x) => f(x) + k);
  }
  const twice = <T,>(x: T): T[] => [x, x];
  return addK(twice(k), (x) => x * 10);
}
console.log(outer(2));
