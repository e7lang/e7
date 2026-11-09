// third test round of 0.1.3 (absent_generic_constraint): must print what Node.js prints
interface Animal { name: string; legs?: number }
class Fish implements Animal { name = "nemo"; }
function legsOf<T extends Animal>(x: T): number { return x.legs ?? 0; }
console.log(legsOf(new Fish()));
