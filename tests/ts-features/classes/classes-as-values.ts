// @feature: Classes as first-class values (`typeof C`, prototypes)
// @category: Classes
// @status: unsupported
// @why: verifiable bytecode: a class is a compile-time layout, not a run-time object with a prototype
// @instead: factory functions `() => new C()`
// @error: 'typeof' types are not supported
class Dog { name = "rex"; }
function create(c: typeof Dog): Dog { return new c(); }
console.log(create(Dog).name);
