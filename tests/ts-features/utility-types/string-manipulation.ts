// @feature: Intrinsic string types (`Uppercase`, `Lowercase`, `Capitalize`, `Uncapitalize`)
// @category: Utility Types
// @status: supported
// @note: `Uppercase` / `Lowercase` / `Capitalize` / `Uncapitalize` of string literal types (ASCII letters; of `string`, `string`)
type Greeting = "Hello, world";
type Shout = Uppercase<Greeting>;
const s: Shout = "HELLO, WORLD";
console.log(s);
