// @feature: Generic constraints (`T extends ...`)
// @category: Generics
// @status: supported
interface Lengthwise { length: number }
function longest<T extends Lengthwise>(a: T, b: T): T {
  return a.length >= b.length ? a : b;
}
interface Comparable<T> { compareTo(o: T): number }
class Version implements Comparable<Version> {
  constructor(public major: number) {}
  compareTo(o: Version): number { return this.major - o.major; }
}
function max<T extends Comparable<T>>(xs: T[]): T {
  let m = xs[0];
  for (const x of xs) if (x.compareTo(m) > 0) m = x;
  return m;
}
console.log(longest([1, 2], [1, 2, 3]), longest("alice", "bob"), max([new Version(1), new Version(3), new Version(2)]).major);
