// @feature: `unique symbol` types
// @category: Symbols
// @status: unsupported
// @why: types known at compile time: `Symbol` values (SPEC 0.1); property names are fixed
// @instead: string constants or an enum
// @error: 'unique' types are not supported
const key: unique symbol = Symbol("key");
interface Keyed { [key]: number }
const k: Keyed = { [key]: 1 };
console.log(k[key]);
