// @error: E7 applies a parameter's default value only when the argument is left out
function base(x: number = Math.max(1, 2)): number { return x; }
const ou: number | undefined = undefined;
console.log(base(ou));
