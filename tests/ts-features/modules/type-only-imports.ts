// @feature: Type-only imports (`import type`, inline `type` modifiers)
// @category: Modules
// @status: supported
import type { Shape, Sides } from "./_lib/shapes.ts";
import { type Square, PI } from "./_lib/shapes.ts";
const s: Shape = { name: "hex", sides: 6 };
const n: Sides = s.sides;
function size(q: Square): number { return q.size; }
console.log(s, n, PI, typeof size);
