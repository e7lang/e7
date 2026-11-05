// @feature: Generic methods in interfaces and object types
// @category: Generics
// @status: unsupported
// @why: types known at compile time: E7 calls interface methods by name, so one method cannot have several instantiations
// @instead: a generic function, or a method of a generic class
// @error: generic methods in interfaces and object types are not supported
interface Mapper { map<U>(f: (x: number) => U): U[] }
const m: Mapper = { map: <U,>(f: (x: number) => U): U[] => [f(1), f(2)] };
console.log(m.map(x => x * 2));
