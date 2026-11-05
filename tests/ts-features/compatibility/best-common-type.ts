// @feature: Best common type of array literals
// @category: Type Compatibility and Inference
// @status: supported
class Animal { constructor(public name: string) {} }
class Rhino extends Animal { horn = 1; }
class Elephant extends Animal { trunk = 2; }
const zoo: Animal[] = [new Rhino("r"), new Elephant("e")];
const mixed = [1, 2.5, -3];
const opt = ["a", null];
console.log(zoo.map(a => a.name), mixed, opt);
