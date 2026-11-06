// @feature: `ThisType<T>`, `ThisParameterType`, `OmitThisParameter`
// @category: Utility Types
// @status: unsupported
// @why: types known at compile time: `ThisType<T>` types `this` in object literals that are not methods of a known class
// @instead: object literal methods (`this` is the object) or classes
// @error: unknown type 'ThisType'
interface Point { x: number; y: number }
const marker: ThisType<Point> = {};
console.log(typeof marker);
