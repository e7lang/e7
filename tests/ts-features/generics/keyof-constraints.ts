// @feature: `keyof` constraints (`K extends keyof T`)
// @category: Generics
// @status: unsupported
// @why: types known at compile time: property names are not values
// @instead: a `Map<K, V>` or a concrete interface with a `switch` on the name
// @error: 'keyof' constraints are not supported
function getProperty<T, K extends keyof T>(obj: T, key: K): T[K] {
  return obj[key];
}
const x = { a: 1, b: 2 };
console.log(getProperty(x, "a"));
