// @error: cannot call 'greet': it is possibly undefined
interface Greeter { name: string; greet?(): string }
const g: Greeter = { name: "Ann" };
console.log(g.greet());
