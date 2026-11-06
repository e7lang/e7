// classes, generic classes, enums, interfaces and type aliases through `import * as ns`
import * as G from "./_lib/geometry.ts";
import * as H from "./_lib/reexport_hub.ts";

class Square extends G.Shape implements G.Sized {
  static count = 0;
  constructor(public size: number) {
    super("square");
    Square.count++;
  }
  area(): number {
    return this.size * this.size;
  }
}
function describe(s: G.Shape): string {
  return s instanceof Square ? `${s.name}:${s.area()}` : s instanceof G.Shape ? s.name : "?";
}
function unitName(u: G.Unit): string {
  switch (u) {
    case G.Unit.Mm:
      return "millimetre";
    case G.Unit.Cm:
      return "centimetre";
    case G.Unit.M:
      return "metre";
  }
}
const shapes: G.Shape[] = [new Square(3), new G.Shape("blob")];
console.log(shapes.map(describe), Square.count, shapes[0] instanceof G.Shape);
const v: G.default = G.default.zero().plus(new G.default(5, 12));
console.log(v.length(), G.default.created, G.move(), G.moves);
G.default.created = 100;
console.log(G.default.created, H.geo.default.created, new H.geo.default(1, 1) instanceof G.default);
const box: G.Box<number> = new G.Box(20);
const boxes: G.Box<string>[] = [box.map((n) => `#${n + 1}`), new G.Box<string>("z")];
const pair: G.Pair<G.Unit> = [G.Unit.Cm, G.Unit.M];
console.log(boxes.map((b) => b.value), pair, unitName(G.Unit.Mm), unitName(pair[1]));
const d: G.Dir = G.Dir.Down;
const sized: G.Sized = { size: G.Unit.Cm * 2 };
console.log(d, d === G.Dir.Down, sized, H.geo.Dir.Up, H.Level.High);
function shadow(): number {
  const G = { Unit: 7 };
  return G.Unit;
}
console.log(shadow());
