// @feature: Methods
// @category: Classes
// @status: supported
class Point {
  x = 10;
  y = 10;
  scale(n: number): void {
    this.x *= n;
    this.y *= n;
  }
  dist(): number { return Math.hypot(this.x, this.y); }
}
const p = new Point();
p.scale(3);
console.log(p.x, p.y, p.dist().toFixed(3));
