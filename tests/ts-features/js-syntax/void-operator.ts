// @feature: The `void` operator
// @category: JavaScript: Syntax
// @status: supported
// @note: the value is `undefined`; as for `undefined` itself, a variable holding it needs a type annotation
let calls = 0;
function touch(): number { return ++calls; }
const u: number | undefined = void 0;
const w: number | undefined = void touch();
console.log(u, w, calls);
