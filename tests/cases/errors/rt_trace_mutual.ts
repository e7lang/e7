// @runtime-error: more frames of even, odd (recursion)
function even(n: number): boolean { return n === 0 ? abort("bottom") : odd(n - 1); }
function odd(n: number): boolean { return even(n - 1); }
console.log(even(60));
