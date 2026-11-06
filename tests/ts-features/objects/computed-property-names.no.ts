// @feature: Computed property names
// @category: Object Types
// @status: partial
// @why: types known at compile time: property names must be known to fix the object layout
// @instead: `Map<string, V>` for keys computed at run time
// @error: a computed property name must be a constant known at compile time
function make(key: string) {
  return { [key]: 10 };
}
console.log(make("w"));
