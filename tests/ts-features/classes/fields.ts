// @feature: Class fields, initializers and constructors
// @category: Classes
// @status: supported
class Point {
  x = 0;
  y: number;
  label: string | null = null;
  constructor(y: number) {
    this.y = y;
  }
}
const p = new Point(5);
p.x = 2;
console.log(p, p.x + p.y);
