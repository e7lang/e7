// @error: assigning a tuple element needs an integer literal index
const t: [number, number] = [1, 2];
let i = 0;
t[i] = 3;
console.log(t);
