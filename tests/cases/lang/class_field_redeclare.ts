// a subclass declaring a field of its base class again (the same property, set after the base's), and
// the private static members of a generic class used by its instances
class A { name: string = "A"; n = 1; constructor() { console.log("A sees", this.name); } show(): string { return this.name + this.n; } }
class C extends A { name: string = "C"; constructor() { super(); console.log("C sees", this.name); } }
class D extends C { override n = 5; }
const d = new D();
console.log(new C().show(), d.show(), Object.keys(d), d);
class Box<T> {
  static #count = 0;
  #fmt = (x: T): string => "<" + String(x) + ">";
  constructor(public v: T) { Box.#count++; }
  static count(): number { return Box.#count; }
  show(): string { return this.#fmt(this.v); }
}
console.log(new Box(1).show(), new Box("a").show(), Box.count());
