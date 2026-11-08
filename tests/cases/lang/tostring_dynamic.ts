// toString() is called on the run-time object, whatever the static type; valueOf() for +
class A { toString(): string { return "A!"; } }
class Base { }
class Sub extends Base { toString(): string { return "Sub"; } }
interface Named { name: string; toString(): string }
const b: Base = new Sub();
const n: A | null = new A();
const lit = { toString(): string { return "lit"; } };
const named: Named = { name: "p", toString() { return "N:" + this.name; } };
const m = new Map<string, A>([["k", new A()]]);
console.log(`${b}`, String(b), "" + b, `${n}`, `${lit}`, String(named), `${m.get("k")}`);
console.log([new Sub(), new Sub()].join("-"), String([new A()]), [new Sub()].toString());
class Ver { constructor(public n: number) {} toString(): string { return "v" + this.n; } }
console.log([new Ver(3), new Ver(1), new Ver(2)].sort().join(","));
class Money { constructor(public cents: number) {} valueOf(): number { return this.cents; } toString(): string { return "$" + (this.cents / 100).toFixed(2); } }
const price = new Money(250);
let s = "total=";
s += price;
console.log("" + price, `${price}`, String(price), s);
