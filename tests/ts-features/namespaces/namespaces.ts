// @feature: Namespaces (`namespace N { ... }`, nested and dotted names, aliases)
// @category: Namespaces
// @status: unsupported
// @why: not implemented yet: ES modules are the unit of code organization
// @instead: modules (`export` / `import * as N`)
// @error: namespaces are not supported
namespace Validation {
  export interface StringValidator { isAcceptable(s: string): boolean }
  export const numberRegexp = "0123456789";
  export class ZipCodeValidator implements StringValidator {
    isAcceptable(s: string): boolean { return s.length === 5; }
  }
}
namespace Shapes.Polygons {
  export class Triangle { sides = 3; }
}
import Polygons = Shapes.Polygons;
console.log(new Validation.ZipCodeValidator().isAcceptable("12345"), new Polygons.Triangle().sides);
