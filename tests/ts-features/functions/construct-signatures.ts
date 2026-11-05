// @feature: Construct signatures (`new (...) => T`)
// @category: More on Functions
// @status: unsupported
// @why: not implemented yet: classes are not values that can be passed around
// @instead: pass a factory function: `(s: string) => new SomeObject(s)`
// @error: expected ')' but found '('
class SomeObject { constructor(public s: string) {} }
function fn(ctor: new (s: string) => SomeObject): SomeObject {
  return new ctor("hello");
}
console.log(fn(SomeObject).s);
