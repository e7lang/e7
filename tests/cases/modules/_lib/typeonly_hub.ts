// imported with `import { type X }`: the module runs (only the specifier is erased)
console.log("typeonly_hub runs");
export type { Point } from "./typeonly_types.ts";
export class Box { w = 2; }
