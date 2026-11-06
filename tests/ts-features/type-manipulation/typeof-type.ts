// @feature: `typeof` in type positions
// @category: Type Manipulation
// @status: unsupported
// @why: types known at compile time: types derived from values (`typeof x`) are excluded (SPEC 0.1); write the type out
// @instead: name the type with an interface or a type alias
// @error: 'typeof' types are not supported
const defaults = { host: "localhost", port: 80 };
type Options = typeof defaults;
const o: Options = { host: "example.com", port: 443 };
console.log(o);
