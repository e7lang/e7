// @feature: `new.target`
// @category: Classes
// @status: unsupported
// @why: not implemented yet
// @instead: an explicit constructor parameter
// @error: new.target is not supported
class Base {
  constructor() {
    console.log(new.target === Base ? "base" : "derived");
  }
}
new Base();
