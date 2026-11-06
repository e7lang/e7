class Animal { name = "a"; }
class Dog extends Animal { bark(): string { return "woof"; } }
class Shelter {
  adopt(name: string, age: number): Animal { return new Animal(); }
  describe(x: number): string { return "shelter " + x; }
}
class DogShelter extends Shelter {
  adopt(name: string): Dog { return new Dog(); }
  describe(): string { return "dogs"; }
}
const s: Shelter = new DogShelter();
console.log(s.adopt("rex", 2).name, new DogShelter().adopt("x").bark(), s.describe(1));
