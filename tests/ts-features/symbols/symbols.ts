// @feature: `Symbol()` values and symbol-keyed properties
// @category: Symbols
// @status: unsupported
// @why: not implemented yet: property names are fixed identifiers
// @instead: string constants, or a private field
// @error: unknown builtin 'Symbol()'
const id = Symbol("id");
const other = Symbol.for("app.other");
console.log(id.toString(), id.description, other.description);
