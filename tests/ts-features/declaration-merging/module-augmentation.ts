// @feature: Module augmentation (`declare module "./x"`)
// @category: Declaration Merging
// @status: unsupported
// @why: verifiable bytecode: a module's types are fixed where it is declared
// @instead: declare the members in the module itself, or a subclass / extended interface
// @error: only 'declare function' is supported
import { Square } from "./_lib/shapes-ref.ts";
declare module "./_lib/shapes-ref.ts" {
  interface Square { color?: string }
}
const sq = new Square(2);
sq.color = "red";
console.log(sq.color);
