// @feature: Type assertions with `as`
// @category: Everyday Types
// @status: supported
// @note: upcasts, removing `null` and class downcasts are checked at run time; `JSON.parse(text) as T` validates the shape
class Animal { name = "animal"; }
class Dog extends Animal { bark(): string { return "woof"; } }
const a: Animal = new Dog();
const d = a as Dog;
const maybe: string | null = "x";
const s = maybe as string;
const n = JSON.parse("[1,2]") as number[];
console.log(d.bark(), s, n);
