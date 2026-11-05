// @error: rest elements in destructuring assignments are not supported
let a = 0;
let b: number[] = [];
[a, ...b] = [1, 2, 3];
console.log(a, b);
