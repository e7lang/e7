// @feature: Default exports and imports
// @category: Modules
// @status: unsupported
// @why: not implemented yet
// @instead: named exports: `export function hello()` and `import { hello } from "./x"`
// @error: default imports are not supported
import hello from "./_lib/default-export.ts";
console.log(hello());
