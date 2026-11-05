// @feature: Template literal types
// @category: Type Manipulation
// @status: unsupported
// @why: not implemented yet: string literal types are treated as `string`
// @instead: `string` with run-time checks (`startsWith`, ...)
// @error: expected type
type Greeting = `hello ${string}`;
const g: Greeting = "hello world";
console.log(g);
