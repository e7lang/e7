// @requires-vm: 0.3.1
// Object literals with accessors and types: a literal implements the accessors an interface declares
// (`get x(): T` / `set x(v: T)`), so it can be passed to functions typed with the interface, stored
// with class instances implementing it, and used through generic interfaces and functions.

interface Shape {
  readonly kind: string;
  get area(): number;
  get perimeter(): number;
}
function describe(s: Shape): string {
  return `${s.kind}: area ${s.area.toFixed(2)}, perimeter ${s.perimeter.toFixed(2)}`;
}
class Square implements Shape {
  kind = "square";
  side: number;
  constructor(side: number) {
    this.side = side;
  }
  get area(): number {
    return this.side ** 2;
  }
  get perimeter(): number {
    return 4 * this.side;
  }
}
function circle(r: number): Shape {
  return {
    kind: "circle",
    get area() {
      return Math.PI * r * r;
    },
    get perimeter() {
      return 2 * Math.PI * r;
    },
  };
}
const shapes: Shape[] = [circle(1), new Square(3), circle(2)];
for (const s of shapes) console.log(describe(s));
console.log(shapes.map((s) => s.area).reduce((a, b) => a + b, 0).toFixed(3));
// a literal's own type is also assignable to the interface
const rect = {
  kind: "rect",
  w: 2,
  h: 5,
  get area() {
    return this.w * this.h;
  },
  get perimeter() {
    return 2 * (this.w + this.h);
  },
};
shapes.push(rect);
console.log(describe(rect), shapes.length);
rect.w = 4;
console.log(describe(shapes[3]));

// getter and setter declared by an interface; `this` is the interface type
interface Temperature {
  celsius: number;
  get fahrenheit(): number;
  set fahrenheit(v: number);
}
const t: Temperature = {
  celsius: 0,
  get fahrenheit() {
    return this.celsius * 1.8 + 32;
  },
  set fahrenheit(v) {
    this.celsius = (v - 32) / 1.8;
  },
};
function heat(x: Temperature, by: number): void {
  x.fahrenheit += by;
}
heat(t, 9);
console.log(t.celsius, t.fahrenheit);

// generic interfaces and functions
interface Cell<T> {
  get value(): T;
  set value(v: T);
  get version(): number;
}
function cell<T>(init: T): Cell<T> {
  let v = init;
  let version = 0;
  return {
    get value() {
      return v;
    },
    set value(x) {
      v = x;
      version++;
    },
    get version() {
      return version;
    },
  };
}
const names = cell("ann");
names.value = names.value + " lee";
const nums = cell<number>(1);
nums.value *= 5;
nums.value += 1;
console.log(names.value, names.version, nums.value, nums.version);
function readAll<T>(cells: Cell<T>[]): T[] {
  return cells.map((c) => c.value);
}
console.log(readAll([nums, cell(42)]));
function first<T>(xs: T[]): T {
  return xs[0];
}
const lits = [
  { name: "a", get len() { return this.name.length; } },
  { name: "bcd", get len() { return this.name.length; } },
];
console.log(first(lits).len, lits.map((x) => x.len));

// optional chaining on an accessor
function areaOf(s: Shape | null): string {
  return s?.area.toFixed(1) ?? "none";
}
console.log(areaOf(circle(3)), areaOf(null));

// an object literal with accessors held by a class field
interface Clock {
  get now(): number;
}
class Timer {
  ticks = 0;
  clock: Clock;
  constructor() {
    const self = this;
    this.clock = {
      get now() {
        return self.ticks * 10;
      },
    };
  }
  tick(): void {
    this.ticks++;
  }
}
const timer = new Timer();
timer.tick();
timer.tick();
console.log(timer.clock.now);
