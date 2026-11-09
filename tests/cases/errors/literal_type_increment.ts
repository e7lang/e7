// @error: cannot be applied to a value of the literal type
type N = 1 | 2;
let n: N = 1;
n++;
console.log(n);
