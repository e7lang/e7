// @feature: Default type parameters
// @category: Generics
// @status: supported
class Container<T = string> {
  items: T[] = [];
  add(x: T): number { return this.items.push(x); }
}
const c = new Container();
c.add("a");
const n = new Container<number>();
n.add(4);
console.log(c.items, n.items);
