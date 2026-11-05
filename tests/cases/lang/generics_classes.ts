// Generics: inherited generic constructors, statics shared by all instances, tryParse, generic
// aliases with defaults, generic methods with their own type parameters.
import { tryParse } from "e7:json";

class Base<T> {
  constructor(public v: T) {}
}
class Sub<T> extends Base<T> {
  twice(): T[] {
    return [this.v, this.v];
  }
}
console.log(new Sub(1).twice(), new Sub("a").twice());

// accessors, static blocks and statics shared by all instances
class Cell<T> {
  static created = 0;
  static {
    Cell.created = 100;
  }
  #v: T;
  constructor(v: T) {
    this.#v = v;
    Cell.created++;
  }
  get value(): T {
    return this.#v;
  }
  set value(v: T) {
    this.#v = v;
  }
}
const cn = new Cell(1);
cn.value += 41;
const cs = new Cell("a");
cs.value += "b";
console.log(cn.value, cs.value, Cell.created);

// tryParse with an instantiated interface
interface Envelope<T> {
  kind: string;
  data: T;
}
const ok = tryParse('{"kind":"k","data":"x"}') as Envelope<string> | null;
const bad = tryParse('{"kind":') as Envelope<string> | null;
console.log(ok === null ? "null" : ok.data, bad === null);

// nested generic arrow capturing locals, called from a nested closure
function make(prefix: string): string[] {
  const tag = <T,>(x: T): string => `${prefix}:${x}`;
  const inner = (): string[] => [tag(1), tag("s"), tag(true)];
  return inner();
}
console.log(make("p"));

// generic aliases with defaults and nested generics
type Maybe<T = number> = T | null;
type ListOf<T> = T[];
function compact<T>(xs: ListOf<Maybe<T>>): T[] {
  const out: T[] = [];
  for (const x of xs) if (x !== null) out.push(x);
  return out;
}
const m1: Maybe = 3;
console.log(compact([1, null, 2]), compact(["a", null]), m1);

// generic class used as a Map value and in records
class Pair2<A, B> {
  constructor(public a: A, public b: B) {}
  swap(): Pair2<B, A> {
    return new Pair2(this.b, this.a);
  }
}
const reg = new Map<string, Pair2<number, string>>();
reg.set("x", new Pair2(1, "one"));
const p2 = reg.get("x");
console.log(p2?.swap(), { pair: new Pair2(true, [1]) });

// a generic function calling another generic function and a generic method
function zip<A, B>(as: A[], bs: B[]): Pair2<A, B>[] {
  const n = Math.min(as.length, bs.length);
  return Array.from({ length: n }, (_, i) => new Pair2(as[i], bs[i]));
}
console.log(zip([1, 2, 3], ["a", "b"]).map((p) => p.swap()));

// generic method in a generic class with its own type parameter
class List<T> {
  constructor(public items: T[]) {}
  map<U>(f: (x: T) => U): List<U> {
    return new List(this.items.map(f));
  }
  reduce<A>(f: (acc: A, x: T) => A, init: A): A {
    let acc = init;
    for (const x of this.items) acc = f(acc, x);
    return acc;
  }
}
const l = new List([1, 2, 3]);
console.log(l.map((x) => x * 1.5).map((x) => String(x)).items, l.reduce((a, x) => a + x, ""));
