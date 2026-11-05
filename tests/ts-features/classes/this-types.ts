// @feature: Polymorphic `this` types
// @category: Classes
// @status: unsupported
// @why: not implemented yet
// @instead: return the class type (and override in subclasses where needed)
// @error: unknown type 'this'
class Box {
  contents = "";
  set(value: string): this {
    this.contents = value;
    return this;
  }
}
class ClearableBox extends Box {
  clear(): void { this.contents = ""; }
}
const b = new ClearableBox().set("hello");
console.log(b.contents);
