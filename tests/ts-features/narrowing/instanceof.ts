// @feature: `instanceof` narrowing
// @category: Narrowing
// @status: supported
class Shape { area(): number { return 0; } }
class Circle extends Shape {
  constructor(public r: number) { super(); }
  area(): number { return Math.PI * this.r ** 2; }
}
class Square extends Shape {
  constructor(public s: number) { super(); }
  area(): number { return this.s * this.s; }
}
function describe(s: Shape): string {
  if (s instanceof Circle) return `circle r=${s.r}`;
  if (s instanceof Square) return `square s=${s.s}`;
  return "shape";
}
const shapes: Shape[] = [new Circle(1), new Square(2), new Shape()];
for (const s of shapes) console.log(describe(s), s.area().toFixed(2));
