// @feature: Structural typing (classes and object literals assigned to interfaces)
// @category: Type Compatibility and Inference
// @status: supported
interface Pet { name: string }
class Dog { name = "Rex"; owner = "Ann"; }
let pet: Pet = { name: "Tom" };
console.log(pet.name);
pet = new Dog();
function greet(p: Pet): string { return "Hi " + p.name; }
console.log(greet(pet), greet(new Dog()));
