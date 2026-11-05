// @feature: `ThisType<T>`, `ThisParameterType`, `OmitThisParameter`
// @category: Utility Types
// @status: unsupported
// @why: not implemented yet: functions cannot be called with a chosen `this`
// @instead: object literal methods (`this` is the object) or classes
// @error: unknown type 'ThisType'
interface Point { x: number; y: number }
const marker: ThisType<Point> = {};
console.log(typeof marker);
