// @feature: Array destructuring (defaults, holes, rest, nested)
// @category: JavaScript: Syntax
// @status: supported
const input = [1, 2, 3, 4];
const [first, second, ...rest] = input;
const [, , third = 0, , fifth = 5] = input;
const [[a, b], [c]] = [[1, 2], [3]];
const [n, s] = [1, "x"];
console.log(first, second, rest, third, fifth, a + b + c, n, s);
