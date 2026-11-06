// @feature: `Symbol()` values and symbol-keyed properties
// @category: Symbols
// @status: unsupported
// @why: types known at compile time: `Symbol` values as property keys would make an object's layout depend on run-time values (SPEC 0.1)
// @instead: string constants, or a private field
// @error: 'Symbol' is not supported
const id = Symbol("id");
const other = Symbol.for("app.other");
console.log(id.toString(), id.description, other.description);
