// @feature: `Record<K, V>`
// @category: Utility Types
// @status: unsupported
// @why: types known at compile time: properties are fixed fields
// @instead: `Map<K, V>`, or an interface with the known keys
// @error: utility type 'Record' is not supported
const ages: Record<string, number> = { ann: 30 };
ages["bob"] = 25;
console.log(ages);
