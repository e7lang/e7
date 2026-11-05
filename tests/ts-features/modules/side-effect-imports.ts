// @feature: Side-effect imports (`import "./x"`)
// @category: Modules
// @status: unsupported
// @why: not implemented yet
// @instead: import a function from the module and call it
// @error: side-effect imports are not supported
import "./_lib/counter.ts";
console.log("imported");
