// `new C(...)` from another module: the constructor's parameter types are resolved in the class's module,
// even when the caller has a type of the same name (or none at all).
import { Graph } from "./_lib/ctor_types.ts";
interface Def { other: number }
const d: Def = { other: 1 };
function make(): Graph { return new Graph({ name: "a", tags: [] }); }
console.log(make().label, new Graph({ name: "b", tags: ["x", "y"] }, { sep: ":" }).label, d.other);
