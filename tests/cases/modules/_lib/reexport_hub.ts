// re-exports: named (renamed, default), star (an explicit export wins over the star exports' `clash`),
// namespace, and a star of a star
export * from "./reexport_a.ts";
export * from "./reexport_b.ts";
export { default as aDefault, fromA as renamedA } from "./reexport_a.ts";
export { default } from "./geometry.ts";
export * as geo from "./geometry.ts";
export * from "./math_utils.ts";
export type { Sized } from "./geometry.ts";
export const clash = "clash from hub";
console.log("init reexport_hub");
