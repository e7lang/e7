// @error: property 'nmae' does not exist on type 'User'; did you mean 'name'?
interface User { name: string; age: number }
const u: User = { name: "a", age: 1 };
console.log(u.nmae);
