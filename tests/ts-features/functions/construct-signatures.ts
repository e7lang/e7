// @feature: Construct signatures (`new (...) => T`)
// @category: More on Functions
// @status: unsupported
// @why: types known at compile time: classes are not values (constructor types would need run-time class values)
// @instead: pass a factory function: `(s: string) => new SomeObject(s)`
// @error: constructor types ('new (...) => T') are not supported
class SomeObject { constructor(public s: string) {} }
function fn(ctor: new (s: string) => SomeObject): SomeObject {
  return new ctor("hello");
}
console.log(fn(SomeObject).s);
