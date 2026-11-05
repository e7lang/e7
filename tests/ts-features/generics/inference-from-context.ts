// @feature: Type argument inference from arguments, callbacks and the expected type
// @category: Generics
// @status: supported
class Box<T> { constructor(public items: T) {} }
function wrap<T>(xs: T[]): Box<T[]> { return new Box(xs); }
const b: Box<string[]> = new Box([]);
b.items.push("a");
function apply<T, R>(x: T, f: (v: T) => R): R { return f(x); }
console.log(wrap([1, 2]).items, b.items, apply("abc", s => s.length), apply(2, n => [n]));
