// @feature: Named exports and imports (`export function/const/class/interface/type/enum`, `import { a, b as c }`)
// @category: Modules
// @status: supported
import { PI, area as circleArea, Square, Kind, describe, explain } from "./_lib/shapes.ts";
import type { Shape } from "./_lib/shapes.ts";
const sq = new Square(3);
const s: Shape = { name: "tri", sides: 3 };
console.log(PI, circleArea(2).toFixed(2), sq.area(), Kind.Angular, describe(sq), explain(s));
