// Method shorthand in object literals: `this` is the object; methods are own properties (shown by
// console.log and Object.keys, skipped by JSON.stringify); interfaces with methods can be
// implemented by object literals.

const calc = {
  n: 2,
  twice(x: number): number {
    return x * this.n;
  },
  describe(): string {
    return `n=${this.n}, twice(5)=${this.twice(5)}`;
  },
};
console.log(calc.twice(21), calc.describe());
calc.n = 10;
console.log(calc.twice(3), calc.describe());
console.log(calc, Object.keys(calc), JSON.stringify(calc));

// inferred return types, arrow functions inside methods keep `this`
const counter = {
  count: 0,
  label: "c",
  inc(by: number) {
    this.count += by;
    return this.count;
  },
  incAll(xs: number[]) {
    xs.forEach((x) => this.inc(x));
    return this.count;
  },
};
console.log(counter.inc(1), counter.incAll([2, 3]), counter.count);

// object literals implementing an interface with methods (shorthand or arrow properties)
interface Shape {
  name: string;
  area(): number;
  scaled(f: number): number;
}
function square(side: number): Shape {
  return {
    name: "square",
    area() {
      return side * side;
    },
    scaled(f) {
      return side * f;
    },
  };
}
const circle = (r: number): Shape => ({
  name: "circle",
  area: () => Math.round(Math.PI * r * r),
  scaled: (f: number) => r * f,
});
const shapes: Shape[] = [square(2), circle(1), square(3)];
for (const s of shapes) console.log(s.name, s.area(), s.scaled(2));

// `this` is the interface type when there is one
interface Account {
  balance: number;
  deposit(x: number): number;
  report(): string;
}
const acct: Account = {
  balance: 5,
  deposit(x: number): number {
    this.balance += x;
    return this.balance;
  },
  report() {
    return "balance " + this.balance + " after " + this.deposit(0);
  },
};
console.log(acct.deposit(10), acct.report());

// every evaluation of the literal creates a new object (and a new `this` for its methods)
function makeCounter(start: number) {
  return {
    v: start,
    next(): number {
      return ++this.v;
    },
  };
}
const c1 = makeCounter(0);
const c2 = makeCounter(100);
c1.next();
c1.next();
console.log(c1.next(), c2.next(), c1.v, c2.v);
const made: { v: number; next(): number }[] = [];
for (let i = 0; i < 3; i++) made.push(makeCounter(i * 10));
console.log(made.map((m) => m.next()));

// closures over local variables, default parameters, assignment to an interface type later
function make(prefix: string) {
  let calls = 0;
  return {
    greet(name: string, punct = "!"): string {
      calls++;
      return `${prefix} ${name}${punct} (${calls})`;
    },
  };
}
const g = make("Hi");
console.log(g.greet("Ann"), g.greet("Bob", "?"));
interface Greeter {
  greet(name: string, punct?: string): string;
}
const gi: Greeter = g;
console.log(gi.greet("Cy"));

// methods that return the object (chaining)
const builder = {
  parts: [] as string[],
  add(p: string) {
    this.parts.push(p);
    return this;
  },
  build(): string {
    return this.parts.join(" ");
  },
};
console.log(builder.add("a").add("b").build());
