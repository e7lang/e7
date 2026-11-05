// @feature: Re-exports (`export { x } from`, `export * from`)
// @category: Modules
// @status: unsupported
// @why: not implemented yet
// @instead: import the names and export them from the module: `import { area } from "./shapes"; export { area };`
// @error: re-exports are not supported
export { area } from "./_lib/shapes.ts";
export * from "./_lib/counter.ts";
console.log("re-exported");
