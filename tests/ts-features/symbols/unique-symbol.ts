// @feature: `unique symbol` types
// @category: Symbols
// @status: unsupported
// @why: not implemented yet
// @instead: string constants or an enum
// @error: 'unique' types are not supported
const key: unique symbol = Symbol("key");
interface Keyed { [key]: number }
const k: Keyed = { [key]: 1 };
console.log(k[key]);
