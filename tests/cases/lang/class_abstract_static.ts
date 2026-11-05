// Abstract classes and members; static blocks (run with the static field initializers, in order).

abstract class Shape {
  abstract readonly kind: string;
  abstract area(): number;
  abstract get sides(): number;
  constructor(public name: string) {}
  describe(): string {
    return `${this.name}: ${this.kind}, area ${this.area().toFixed(2)}, ${this.sides} sides`;
  }
  // a concrete method calling an abstract one through this
  doubled(): number {
    return this.area() * 2;
  }
}

class Rect extends Shape {
  readonly kind = "rect";
  constructor(
    public w: number,
    public h: number,
  ) {
    super("R");
  }
  area(): number {
    return this.w * this.h;
  }
  get sides(): number {
    return 4;
  }
}

// an abstract class extending an abstract class may leave members abstract
abstract class Polygon extends Shape {
  abstract perimeter(): number;
  ratio(): number {
    return this.area() / this.perimeter();
  }
}

class Triangle extends Polygon {
  kind = "triangle";
  constructor(public a: number) {
    super("T");
  }
  area(): number {
    return (Math.sqrt(3) / 4) * this.a * this.a;
  }
  perimeter(): number {
    return 3 * this.a;
  }
  get sides(): number {
    return 3;
  }
}

const shapes: Shape[] = [new Rect(2, 3), new Triangle(2)];
for (const s of shapes) console.log(s.describe(), s.doubled().toFixed(1), s.kind);
const tri = new Triangle(3);
console.log(tri.ratio().toFixed(4), tri.kind, JSON.stringify(tri), Object.keys(tri));
// the abstract property is declared by the subclass: it comes after the base class fields
console.log(new Rect(1, 1));

// an abstract property read and written through the base type
abstract class Named {
  abstract label: string;
  rename(s: string): void {
    this.label = s;
    this.label += "!";
  }
}
class Item extends Named {
  label = "item";
}
const it: Named = new Item();
it.rename("thing");
console.log(it.label);

// static blocks and static initializers run in source order
class Registry {
  static readonly names: string[] = [];
  static first = Registry.add("first");
  static count = -1;
  static {
    Registry.add("block 1");
    Registry.count = Registry.names.length;
  }
  static last = Registry.add("last");
  static {
    const extra = ["x", "y"];
    for (const e of extra) Registry.add("block 2 " + e);
  }
  static add(n: string): number {
    Registry.names.push(n);
    return Registry.names.length;
  }
}
console.log(Registry.names, Registry.first, Registry.count, Registry.last);

// a static block can use the private static members of its class
class Ids {
  static #next = 100;
  static readonly start: number;
  static {
    Ids.#next += 5;
    try {
      Ids.check(-1);
    } catch (e) {
      console.log("static block caught", String(e));
    }
  }
  static next(): number {
    return Ids.#next++;
  }
  static check(n: number): void {
    if (n < 0) throw new RangeError("negative");
  }
}
console.log(Ids.next(), Ids.next());
