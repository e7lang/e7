// @feature: Re-exports (`export { x } from`, `export * from`, `export * as ns from`)
// @category: Modules
// @status: supported
// @note: a module's own exports win over `export *`; a name that two `export *` provide with different bindings is an error, as in TypeScript (JavaScript leaves it out)
import { area, pi, count, increment, shapes, hello } from "./_lib/reexports.ts";
import type { Shape } from "./_lib/reexports.ts";
const s: Shape = new shapes.Square(2);
console.log(area(1).toFixed(2), pi, count, increment(), count, shapes.explain(s), hello());
