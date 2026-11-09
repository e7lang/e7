// @error: property 'legs' does not exist on type 'Fish'
// the class itself does not have the interface's optional property (as in TypeScript)
interface Animal { name: string; legs?: number }
class Fish implements Animal { name = "nemo"; }
console.log(new Fish().legs);
