// @feature: Overriding with a compatible signature (covariant return type, fewer parameters)
// @category: Classes
// @status: unsupported
// @why: not implemented yet: an override must have the same signature as the overridden method
// @instead: keep the base signature (and downcast with `as` at the call site where needed)
// @error: must have the same signature as the overridden method
class Animal { name = "animal"; }
class Dog extends Animal { bark(): string { return "woof"; } }
class Shelter { adopt(): Animal { return new Animal(); } }
class DogShelter extends Shelter {
  adopt(): Dog { return new Dog(); }
}
console.log(new DogShelter().adopt().bark());
