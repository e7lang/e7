// @feature: Side-effect imports (`import "./x"`)
// @category: Modules
// @status: supported
// @note: the module runs once, in JavaScript's order (dependencies first, in source order)
import "./_lib/side-effect.ts";
import { count } from "./_lib/counter.ts";
console.log("imported", count);
