// Generics: classes with #private / accessors, inheritance from and to generic classes, constraints,
// defaults, explicit type arguments, generic functions as values, Map / JSON.parse with instances.
// stack
class Stack<T> {
  #items: T[] = [];
  push(x: T): Stack<T> {
    this.#items.push(x);
    return this;
  }
  pop(): T | undefined {
    return this.#items.pop();
  }
  get size(): number {
    return this.#items.length;
  }
  peek(): T | null {
    return this.#items.length > 0 ? this.#items[this.#items.length - 1] : null;
  }
}
const s = new Stack<number>();
s.push(1).push(2);
console.log(s.size, s.pop(), s.peek(), s);
const ss = new Stack<string>();
ss.push("a");
console.log(ss.peek(), JSON.stringify(ss));

// inheritance from and to generic classes
class Animal {
  constructor(public name: string) {}
}
class Dog extends Animal {
  bark(): string {
    return this.name + " woof";
  }
}
class Container<T> {
  items: T[] = [];
  add(x: T): number {
    return this.items.push(x);
  }
}
class DogPound extends Container<Dog> {
  barkAll(): string[] {
    return this.items.map((d) => d.bark());
  }
}
const pound = new DogPound();
pound.add(new Dog("rex"));
console.log(pound.barkAll(), pound.items.length);
class Keyed<K, V> extends Container<V> {
  constructor(public key: K) {
    super();
  }
  describe(): string {
    return `${this.key}: ${this.items.length}`;
  }
}
const kd = new Keyed<string, number>("k");
kd.add(5);
console.log(kd.describe(), kd);

// generic interface implemented by a generic class
interface Comparable<T> {
  compareTo(other: T): number;
}
class Version implements Comparable<Version> {
  constructor(public major: number, public minor: number) {}
  compareTo(o: Version): number {
    return this.major - o.major || this.minor - o.minor;
  }
}
function maxOf<T extends Comparable<T>>(xs: T[]): T {
  let best = xs[0];
  for (const x of xs) if (x.compareTo(best) > 0) best = x;
  return best;
}
console.log(maxOf([new Version(1, 2), new Version(1, 10), new Version(0, 99)]));

// abstract generic class
abstract class Repo<T> {
  protected rows: T[] = [];
  abstract key(x: T): string;
  save(x: T): void {
    this.rows.push(x);
  }
  find(k: string): T | undefined {
    return this.rows.find((r) => this.key(r) === k);
  }
}
interface User {
  id: string;
  age: number;
}
class UserRepo extends Repo<User> {
  key(u: User): string {
    return u.id;
  }
}
const repo = new UserRepo();
repo.save({ id: "a", age: 3 });
console.log(repo.find("a"), repo.find("b"));

// defaults, explicit arguments, constraints
function fill<T = number>(n: number, v: T): T[] {
  return new Array<T>(n).fill(v);
}
console.log(fill(2, "x"), fill<number>(3, 0));
interface HasLen {
  length: number;
}
function longest<T extends HasLen>(a: T, b: T): T {
  return a.length >= b.length ? a : b;
}
console.log(longest("ab", "abc"), longest([1, 2], [3]));

// null widening
function orDefault<T>(x: T | null, d: T): T {
  return x === null ? d : x;
}
console.log(orDefault(null, 5), orDefault("a", "b"));
function pair<A, B>(a: A, b: B): [A, B] {
  return [a, b];
}
const pr = pair(1, "x");
console.log(pr, pr[1].length);

// generic functions as values
const nums = [1, 2, 3];
function idf<T>(x: T): T {
  return x;
}
console.log(nums.map(idf), ["q"].map(idf));
const toArr = <T,>(x: T): T[] => [x];
console.log(nums.map(toArr));

// closures capturing values of type parameters
function counter<T>(initial: T, next: (x: T) => T): () => T {
  let cur = initial;
  return () => {
    cur = next(cur);
    return cur;
  };
}
const c1 = counter(0, (x) => x + 1);
const c2 = counter("", (x) => x + "a");
c1();
console.log(c1(), c2(), c2());

// Map of generic types
const boxes = new Map<string, Stack<number>>();
boxes.set("a", new Stack<number>().push(4));
console.log(boxes.get("a")?.peek());
function groupBy<T, K>(xs: T[], key: (x: T) => K): Map<K, T[]> {
  const m = new Map<K, T[]>();
  for (const x of xs) {
    const k = key(x);
    const g = m.get(k);
    if (g) g.push(x);
    else m.set(k, [x]);
  }
  return m;
}
console.log(groupBy(["aa", "b", "cc"], (s) => s.length));

// JSON.parse of instantiated types
interface Envelope<T> {
  kind: string;
  data: T;
}
const env = JSON.parse('{"kind":"n","data":[1,2]}') as Envelope<number[]>;
console.log(env.data.length, env);

// finally, destructuring, method shorthand inside generic code
function withLog<T>(f: () => T, log: string[]): T {
  try {
    return f();
  } finally {
    log.push("done");
  }
}
const lg: string[] = [];
console.log(withLog(() => 42, lg), lg);
function firstTwo<T>([a, b = a, ...rest]: T[], d: T): [T, T, number] {
  return [a, b, rest.length + (d === a ? 1 : 0)];
}
console.log(firstTwo([1], 0), firstTwo(["x", "y", "z"], ""));
function holder<T>(v: T) {
  return {
    v,
    get2(): T[] {
      return [this.v, this.v];
    },
  };
}
console.log(holder(3).get2(), holder("s").get2());

// recursion with a declared return type
function flattenDeep<T>(xs: T[][]): T[] {
  return xs.length === 0 ? [] : xs[0].concat(flattenDeep(xs.slice(1)));
}
console.log(flattenDeep([[1], [2, 3]]));

// typeof is per instance
function kind<T>(x: T): string {
  return typeof x;
}
console.log(kind(1), kind("s"), kind(true));

// generic static method and generic method with explicit type arguments
class Util {
  static wrap<T>(x: T): T[] {
    return [x];
  }
  conv<T>(x: number, f: (n: number) => T): T {
    return f(x);
  }
}
console.log(Util.wrap(1), Util.wrap<string>("a"), new Util().conv<string>(5, (n) => "n" + n));
