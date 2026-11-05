// Getters and setters (compiled to method calls): instance and static accessors, inheritance and
// overriding, super.x, compound assignment, interfaces that declare accessors.

class Temperature {
  private celsius = 0;
  get fahrenheit(): number {
    return this.celsius * 1.8 + 32;
  }
  set fahrenheit(f: number) {
    this.celsius = (f - 32) / 1.8;
  }
  get kelvin() {
    return this.celsius + 273.15;
  }
  get c(): number {
    return this.celsius;
  }
  set c(v) {
    this.celsius = v;
  }
}
const t = new Temperature();
console.log(t.fahrenheit, t.kelvin);
t.fahrenheit = 212;
console.log(t.c, t.fahrenheit);
t.c += 10;
console.log(t.c);
t.c++;
++t.c;
console.log(t.c, t.c--, t.c);
t.c *= 2;
console.log(t.c);
// an assignment expression has the assigned value
console.log((t.c = 5), t.c);

// accessors are not own properties: JSON / Object.keys / console.log show only fields
console.log(JSON.stringify(t), Object.keys(t), t);

// side effects happen once per access, in order
class Counter {
  reads = 0;
  writes = 0;
  private v = 1;
  get value(): number {
    this.reads++;
    return this.v;
  }
  set value(x: number) {
    this.writes++;
    this.v = x;
  }
}
const cnt = new Counter();
cnt.value += 5;
cnt.value **= 2;
console.log(cnt.value, cnt.reads, cnt.writes);
let calls = 0;
const get = (): Counter => {
  calls++;
  return cnt;
};
get().value -= 1;
console.log(cnt.value, calls);

// inheritance: getters are virtual; super.x calls the base accessor
class Shape {
  constructor(public name: string) {}
  get area(): number {
    return 0;
  }
  get label(): string {
    return `${this.name} (${this.area.toFixed(2)})`;
  }
}
class Circle extends Shape {
  constructor(public r: number) {
    super("circle");
  }
  override get area(): number {
    return Math.PI * this.r * this.r;
  }
}
class Square extends Shape {
  private _side = 0;
  constructor(side: number) {
    super("square");
    this.side = side;
  }
  get side(): number {
    return this._side;
  }
  set side(s: number) {
    if (s < 0) throw new RangeError("negative side");
    this._side = s;
  }
  override get area(): number {
    return this.side ** 2;
  }
  override get label(): string {
    return "[" + super.label + "]";
  }
}
const shapes: Shape[] = [new Circle(1), new Square(3), new Shape("blob")];
for (const s of shapes) console.log(s.label, s.area);
const sq = new Square(2);
sq.side = 5;
console.log(sq.area);
try {
  sq.side = -1;
} catch (e) {
  console.log(String(e), sq.side);
}

// a setter in a subclass that calls the base setter
class Base {
  protected log: string[] = [];
  private _v = "";
  get v(): string {
    return this._v;
  }
  set v(x: string) {
    this.log.push("base " + x);
    this._v = x;
  }
  history(): string {
    return this.log.join(", ");
  }
}
class Derived extends Base {
  override get v(): string {
    return super.v.toUpperCase();
  }
  override set v(x: string) {
    this.log.push("derived " + x);
    super.v = x + "!";
  }
}
const dv = new Derived();
dv.v = "hi";
console.log(dv.v, dv.history());

// static accessors
class Config {
  static #level = 1;
  static get level(): number {
    return Config.#level;
  }
  static set level(l: number) {
    Config.#level = Math.max(0, Math.min(5, l));
  }
}
Config.level = 9;
console.log(Config.level);
Config.level -= 10;
console.log(Config.level);

// interfaces that declare accessors
interface Sized {
  get size(): number;
  set size(n: number);
  readonly name: string;
}
class Box implements Sized {
  name = "box";
  private n = 1;
  get size(): number {
    return this.n;
  }
  set size(v: number) {
    this.n = v;
  }
}
const sz: Sized = new Box();
sz.size = 4;
sz.size *= 3;
console.log(sz.size, sz.name);

// narrowing of a nullable getter
class Holder {
  private _x: number | undefined = 3;
  get x(): number | undefined {
    return this._x;
  }
}
const ho = new Holder();
if (ho.x !== undefined) console.log(ho.x + 1);
const maybeHolder: Holder | null = Math.random() < 2 ? ho : null;
console.log(maybeHolder?.x);

// destructuring calls getters; a rest object has only the own fields
const { side: sd, area: ar, ...own } = new Square(4);
console.log(sd, ar, own);

// try / finally inside a getter
class Logged {
  log: string[] = [];
  get x(): number {
    try {
      return 1;
    } finally {
      this.log.push("got");
    }
  }
}
const lg = new Logged();
console.log(lg.x + lg.x, lg.log);
