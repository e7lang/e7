// @feature: Overriding with a compatible signature (covariant return type, fewer parameters)
// @category: Classes
// @status: supported
// @note: parameters and return types with the overridden method's representation (a class instead of its base class, fewer parameters)
class Animal { name = "animal"; }
class Dog extends Animal { bark(): string { return "woof"; } }
class Shelter {
  adopt(): Animal { return new Animal(); }
  describe(id: number): string { return "shelter " + id; }
}
class DogShelter extends Shelter {
  adopt(): Dog { return new Dog(); }
  describe(): string { return "dog shelter"; }
}
const s: Shelter = new DogShelter();
console.log(new DogShelter().adopt().bark(), s.describe(1));
