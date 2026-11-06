// @feature: Namespace imports (`import * as ns`)
// @category: Modules
// @status: supported
// @note: functions, constants, classes (`new ns.C()`, static members, `instanceof`, `extends`), enums and types (`ns.T`, `ns.Box<number>`) through the namespace; its members cannot be assigned, as in JavaScript
import * as shapes from "./_lib/shapes.ts";
const sq = new shapes.Square(2);
const s: shapes.Shape = sq;
const kind: shapes.Kind = shapes.Kind.Angular;
const n: shapes.Sides = s.sides;
console.log(shapes.area(1).toFixed(3), shapes.PI, s.name, n, kind, sq instanceof shapes.Square, shapes.explain(sq));
