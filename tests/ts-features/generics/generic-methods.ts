// @feature: Generic methods in classes
// @category: Generics
// @status: supported
// @note: generic methods are called directly (they cannot override or be overridden)
class Box<T> {
  constructor(public value: T) {}
  map<U>(f: (x: T) => U): Box<U> { return new Box(f(this.value)); }
  static of<V>(v: V): Box<V> { return new Box(v); }
}
const b = Box.of(21).map(x => x * 2).map(x => `v=${x}`);
console.log(b.value, b);
