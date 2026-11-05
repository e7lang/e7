// @feature: `for...in`
// @category: JavaScript: Syntax
// @status: unsupported
// @why: types known at compile time: it enumerates property names as untyped strings, including inherited ones
// @instead: `for (const k of Object.keys(obj))` or a `Map`
// @error: for...in is not supported
const o = { a: 1, b: 2 };
for (const k in o) console.log(k);
