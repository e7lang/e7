// @feature: Destructuring assignment to existing variables
// @category: JavaScript: Syntax
// @status: partial
// @note: plain array patterns (`[a, b] = [b, a]`)
let a = 1;
let b = 2;
[a, b] = [b, a];
let first = "", second = 0;
[first, second] = ["x", 5];
console.log(a, b, first, second);
