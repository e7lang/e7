// @feature: `keyof` type operator
// @category: Type Manipulation
// @status: unsupported
// @why: types known at compile time: property names are not first-class values
// @instead: an enum or a string field with a `switch`, or `Map<K, V>`
// @error: 'keyof' types are not supported
interface Point { x: number; y: number }
type P = keyof Point;
const k: P = "x";
console.log(k);
