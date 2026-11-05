// @feature: Abstract classes, methods and properties
// @category: Classes
// @status: supported
abstract class Base {
  abstract kind: string;
  abstract getName(): string;
  printName(): string { return `Hello, ${this.getName()} (${this.kind})`; }
}
class Derived extends Base {
  kind = "derived";
  getName(): string { return "world"; }
}
const b: Base = new Derived();
console.log(b.printName());
