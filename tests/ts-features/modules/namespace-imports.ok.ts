// @feature: Namespace imports (`import * as ns`)
// @category: Modules
// @status: partial
// @note: functions and constants through the namespace (members cannot be assigned)
import * as shapes from "./_lib/shapes.ts";
import { Square } from "./_lib/shapes.ts";
const sq = new Square(2);
console.log(shapes.area(1).toFixed(3), shapes.PI, shapes.explain(sq), shapes.describe({ name: "tri", sides: 3 }));
