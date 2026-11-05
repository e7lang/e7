// @feature: Circular imports
// @category: Modules
// @status: unsupported
// @why: verifiable bytecode: modules are initialized in dependency order, so every import is initialized before it is used
// @instead: move the shared declarations into a third module
// @error: circular import
import { sumAB } from "./_lib/cycle-a.ts";
console.log(sumAB());
