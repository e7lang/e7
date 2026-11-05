// @error: default values in destructuring assignments are not supported
let a = 0;
let b = 0;
[a, b = 5] = [1];
console.log(a, b);
