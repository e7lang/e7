// @feature: The `override` modifier
// @category: Classes
// @status: supported
class Base { greet(): string { return "base"; } }
class Derived extends Base {
  override greet(): string { return "derived+" + super.greet(); }
}
console.log(new Derived().greet());
