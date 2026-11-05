// @feature: Generic function types `<T>(x: T) => T`
// @category: Generics
// @status: unsupported
// @why: types known at compile time: a function value has one concrete signature
// @instead: a concrete function type, or a generic function called directly
// @error: generic function types (<T>(x: T) => T) are not supported
type Identity = <T>(x: T) => T;
const id: Identity = x => x;
console.log(id(1), id("a"));
