// @feature: Accessors declared in interfaces (`get x(): T`)
// @category: Object Types
// @status: supported
// @note: an interface *property* is read as a field, so a class implements it with a field; declare accessors as `get` / `set` in the interface
interface Sized { get size(): number }
class Bag implements Sized {
  items: string[] = [];
  get size(): number { return this.items.length; }
}
const b = new Bag();
b.items.push("x", "y");
const s: Sized = b;
console.log(s.size);
