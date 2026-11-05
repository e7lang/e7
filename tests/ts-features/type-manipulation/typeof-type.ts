// @feature: `typeof` in type positions
// @category: Type Manipulation
// @status: unsupported
// @why: not implemented yet: types are written out, not derived from values
// @instead: name the type with an interface or a type alias
// @error: 'typeof' types are not supported
const defaults = { host: "localhost", port: 80 };
type Options = typeof defaults;
const o: Options = { host: "example.com", port: 443 };
console.log(o);
