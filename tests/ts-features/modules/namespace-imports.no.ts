// @feature: Namespace imports (`import * as ns`)
// @category: Modules
// @status: partial
// @why: not implemented yet: classes, types and enums through a namespace (`new ns.C()`, `ns.T`, `ns.E.A`; docs/SPEC.md section 7 lists `import * as ns` without the restriction)
// @instead: import those names directly: `import { Square, Kind } from "./shapes"`
// @error: classes cannot be constructed through a namespace import
import * as shapes from "./_lib/shapes.ts";
const sq = new shapes.Square(2);
const s: shapes.Shape = sq;
console.log(s.name, shapes.Kind.Round);
