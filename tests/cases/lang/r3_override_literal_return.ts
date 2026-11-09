// third test round of 0.1.3 (override_literal_return): must print what Node.js prints
abstract class A {
  abstract max(): number;
  name(): string { return "a"; }
  flag(): boolean { return false; }
}
class B extends A {
  max() { return 5; }
  name() { return "b"; }
  flag() { return true; }
}
const a: A = new B();
console.log(a.max(), a.name(), a.flag());
