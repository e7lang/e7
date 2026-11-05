// @feature: `instanceof` with a generic class
// @category: Generics
// @status: unsupported
// @why: types known at compile time: E7 compiles one class per type argument list, so there is no single class to test against
// @instead: test a non-generic base class, or keep the static type
// @error: is a generic class, and e7 compiles one class per type argument list
class Box<T> { constructor(public v: T) {} }
const b: Box<number> = new Box(1);
console.log(b instanceof Box);
