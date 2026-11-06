// @feature: Polymorphic `this` types
// @category: Classes
// @status: supported
// @note: a method returning `this` gives the receiver's class at a call (method chaining in subclasses)
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
