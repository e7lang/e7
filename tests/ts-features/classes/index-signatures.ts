// @feature: Index signatures in classes
// @category: Classes
// @status: unsupported
// @why: types known at compile time: class layouts are fixed
// @instead: a `Map<string, T>` field
// @error: index signatures are not supported
class MyClass {
  [s: string]: boolean | ((s: string) => boolean);
  check(s: string): boolean { return this[s] as boolean; }
}
console.log(new MyClass().check("x"));
