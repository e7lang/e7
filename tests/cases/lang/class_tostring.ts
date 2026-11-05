// String(x), template literals and concatenation call a class's own toString(), as in JavaScript
class P {
  x: number;
  constructor(x: number) { this.x = x; }
  toString(): string { return "P(" + this.x + ")"; }
}
class Q extends P {
  toString(): string { return "Q<" + super.toString() + ">"; }
}
class R extends P {}
class N { y = 1; }
const p = new P(1);
const q: P = new Q(2);
console.log(String(p), `${q}`, "v=" + new R(3), p + "!", String(new N()), `${new N()}`);
