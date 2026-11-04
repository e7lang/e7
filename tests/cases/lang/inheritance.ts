abstract_free();
function abstract_free(): void {}
class Shape {
  static created = 0;
  constructor(public name: string) { Shape.created++; }
  area(): number { return 0; }
  describe(): string { return `${this.name} area=${this.area().toFixed(2)}`; }
  static unit(): Shape { return new Square(1); }
}
class Rect extends Shape {
  constructor(public w: number, public h: number) { super("rect"); }
  area(): number { return this.w * this.h; }
}
class Square extends Rect {
  constructor(s: number) { super(s, s); this.name = "square"; }
  describe(): string { return "[" + super.describe() + "]"; }
}
class Circle extends Shape {
  constructor(public r: number) { super("circle"); }
  area(): number { return Math.PI * this.r ** 2; }
}
const shapes: Shape[] = [new Rect(2, 3), new Square(4), new Circle(1), Shape.unit()];
for (const s of shapes) console.log(s.describe());
console.log(Shape.created, shapes.filter(s => s instanceof Rect).length);
const total = shapes.reduce((acc, s) => acc + s.area(), 0);
console.log(total.toFixed(3));
interface Visitor { visitNum(n: number): string; visitStr(s: string): string; }
class Upper implements Visitor { visitNum(n: number): string { return "N" + n; } visitStr(s: string): string { return s.toUpperCase(); } }
class Lower implements Visitor { prefix = "~"; visitNum(n: number): string { return this.prefix + n; } visitStr(s: string): string { return this.prefix + s.toLowerCase(); } }
const visitors: Visitor[] = [new Upper(), new Lower(), new Upper()];
for (const v of visitors) console.log(v.visitNum(3), v.visitStr("MiXeD"));
interface HasArea { area(): number; name: string }
function biggest(xs: HasArea[]): string {
  let best = xs[0];
  for (const x of xs) if (x.area() > best.area()) best = x;
  return best.name;
}
console.log(biggest(shapes));
const sq = shapes[1] as Square;
console.log(sq.w, sq.h);
class Base2 { greet(x: string = "you"): string { return "hello " + x; } }
class Derived2 extends Base2 { greet(x: string = "them"): string { return super.greet(x) + "!"; } }
const b2: Base2 = new Derived2();
console.log(b2.greet(), b2.greet("me"));
