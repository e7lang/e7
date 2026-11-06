// @feature: Template literal types
// @category: Type Manipulation
// @status: unsupported
// @why: types known at compile time: template literal types are excluded (SPEC 0.1); use a union of literals
// @instead: `string` with run-time checks (`startsWith`, ...)
// @error: expected type
type Greeting = `hello ${string}`;
const g: Greeting = "hello world";
console.log(g);
