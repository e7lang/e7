// @error: nested patterns in rest elements are not supported
const [a, ...[b, c]] = [1, 2, 3];
console.log(a, b, c);
