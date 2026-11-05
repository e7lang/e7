// Generics: recursion, self-referential and mutually recursive generic classes, abstract generic
// bases (CRTP), closures over type-parameter values, optional / default parameters.
console.log(len([1, 2, 3]), len(["a"]));
function len<T>(xs: T[]): number {
  return xs.length === 0 ? 0 : 1 + len(xs.slice(1));
}

class ListNode<T> {
  next: ListNode<T> | null = null;
  constructor(public value: T) {}
  append(v: T): ListNode<T> {
    const n = new ListNode(v);
    this.next = n;
    return n;
  }
}
const head = new ListNode(1);
head.append(2).append(3);
const vals: number[] = [];
for (let n: ListNode<number> | null = head; n !== null; n = n.next) vals.push(n.value);
console.log(vals);

class TreeA<T> {
  kids: TreeB<T>[] = [];
  constructor(public v: T) {}
}
class TreeB<T> {
  parent: TreeA<T> | null = null;
  constructor(public w: T) {}
}
const ta = new TreeA("root");
const tb = new TreeB("kid");
tb.parent = ta;
ta.kids.push(tb);
console.log(ta.kids[0].parent?.v, tb.parent === ta);

abstract class Comparable<T> {
  abstract cmp(o: T): number;
  max(o: T): T | null {
    return this.cmp(o) >= 0 ? null : o;
  }
}
class Money extends Comparable<Money> {
  constructor(public cents: number) {
    super();
  }
  cmp(o: Money): number {
    return this.cents - o.cents;
  }
}
console.log(new Money(5).max(new Money(9))?.cents, new Money(9).max(new Money(5)));

function sortBy<T>(xs: T[], key: (x: T) => number): T[] {
  return [...xs].sort((a, b) => key(a) - key(b));
}
console.log(sortBy(["ccc", "a", "bb"], (s) => s.length), sortBy([{ n: 2 }, { n: 1 }], (o) => o.n));

function memo<A, R>(f: (a: A) => R): (a: A) => R {
  const cache = new Map<A, R>();
  return (a: A): R => {
    const hit = cache.get(a);
    if (hit !== undefined) return hit;
    const r = f(a);
    cache.set(a, r);
    return r;
  };
}
let calls = 0;
const sq = memo((x: number) => {
  calls++;
  return x * x;
});
console.log(sq(3), sq(3), sq(4), calls);
const up = memo((s: string) => s.toUpperCase());
console.log(up("a"));

function entriesOf<V>(m: Map<string, V>): [string, V][] {
  return [...m.entries()];
}
console.log(entriesOf(new Map<string, number>([["k", 1]])));

function opt<T>(x?: T): string {
  return x === undefined ? "none" : `${x}`;
}
console.log(opt<number>(), opt(5));

function firstOr<T>(xs: T[], d: T = xs[0]): T {
  return xs.length > 1 ? xs[1] : d;
}
console.log(firstOr([7]), firstOr(["a", "b"]));
