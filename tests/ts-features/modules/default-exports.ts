// @feature: Default exports and imports (`export default`, `import x from`)
// @category: Modules
// @status: supported
// @note: `export default` of a function, class, interface or expression (evaluated once, where it stands), `export { x as default }`, `import x from`, `import x, { a } from`, `import { default as x } from`
import hello, { greeting } from "./_lib/default-export.ts";
import { default as hi } from "./_lib/default-export.ts";
import Point from "./_lib/default-class.ts";
import answer from "./_lib/default-value.ts";
console.log(hello(), hi(), greeting, new Point(3, 4).length(), answer);
