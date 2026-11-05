// @feature: Computed property names
// @category: Object Types
// @status: unsupported
// @why: types known at compile time: property names must be known to fix the object layout
// @instead: `Map<string, V>` for dynamic keys, or literal property names
// @error: computed property names are not supported
const key = "width";
const box = { [key]: 10, height: 5 };
console.log(box);
