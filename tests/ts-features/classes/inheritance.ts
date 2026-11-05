// @feature: `extends`, `super` calls and method overriding
// @category: Classes
// @status: supported
class Animal {
  constructor(public name: string) {}
  move(distance = 0): string { return `${this.name} moved ${distance}m`; }
}
class Snake extends Animal {
  constructor(name: string) { super(name + " the snake"); }
  move(distance = 5): string { return "Slithering... " + super.move(distance); }
}
class Horse extends Animal {}
const animals: Animal[] = [new Snake("Sammy"), new Horse("Tommy")];
for (const a of animals) console.log(a.move());
console.log(animals[1].move(34), animals[0] instanceof Animal);
