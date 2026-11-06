// @feature: Type predicates (`x is T`)
// @category: Narrowing
// @status: supported
// @note: user-defined type guards narrow nullable values, literal types and class types (as `instanceof` does); the function's check is trusted, as in TypeScript
function isDefined(s: string | undefined): s is string {
  return s !== undefined;
}
class Animal { constructor(public name: string) {} }
class Dog extends Animal { bark(): string { return "woof"; } }
function isDog(a: Animal): a is Dog { return a instanceof Dog; }
const v: string | undefined = "a";
const pet: Animal = new Dog("rex");
if (isDefined(v)) console.log(v.length);
if (isDog(pet)) console.log(pet.bark());
