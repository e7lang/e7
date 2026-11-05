// @error: abstract methods can only appear within an abstract class
class A {
  abstract f(): number;
}
console.log(new A());
