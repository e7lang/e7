// with nothing to infer from, a type parameter takes its default, else its constraint (as in tsc)
class Shape {
  r: number;
  constructor(r: number) { this.r = r; }
  area(): number { return 3 * this.r * this.r; }
}
function make<T extends Shape>(): T[] { return []; }
function firstOr<T = string>(xs: T[], d: T): T { return xs.length > 0 ? xs[0] : d; }
const shapes = make();
shapes.push(new Shape(2));
console.log(shapes.length, shapes[0].area(), firstOr([], "none"));
