// @error: not assignable
interface A { x: number }
const s = { x: "a" };
const a: A = { ...s };
console.log(a.x);
