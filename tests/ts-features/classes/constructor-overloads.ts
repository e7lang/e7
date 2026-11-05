// @feature: Constructor overloads
// @category: Classes
// @status: unsupported
// @why: types known at compile time: the implementation signature needs a union type
// @instead: optional parameters, or static factory methods
// @error: expected '{' but found ';'
class Point {
  x = 0;
  constructor(x: number);
  constructor(s: string);
  constructor(v: number | string) {
    this.x = Number(v);
  }
}
console.log(new Point("4").x);
