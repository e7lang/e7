// @feature: `readonly` fields
// @category: Classes
// @status: supported
// @note: assignable only in the class's own constructor, as in tsc
class Greeter {
  readonly name: string = "world";
  static readonly prefix = "Hello, ";
  constructor(otherName?: string) {
    if (otherName !== undefined) this.name = otherName;
  }
  greet(): string { return Greeter.prefix + this.name; }
}
console.log(new Greeter().greet(), new Greeter("ts").greet());
