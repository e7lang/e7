// @feature: Intrinsic string types (`Uppercase`, `Lowercase`, `Capitalize`, `Uncapitalize`)
// @category: Utility Types
// @status: unsupported
// @why: not implemented yet: string literal types are treated as `string`
// @instead: `string` with `toUpperCase()` / `toLowerCase()` at run time
// @error: unknown type 'Uppercase'
type Greeting = "Hello, world";
type Shout = Uppercase<Greeting>;
const s: Shout = "HELLO, WORLD";
console.log(s);
