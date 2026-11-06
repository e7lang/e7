// @feature: Destructuring assignment to existing variables
// @category: JavaScript: Syntax
// @status: supported
// @note: a pattern with defaults, nesting or rest must be a statement of its own (`[a = 1, ...rest] = xs;`)
let a = 1;
let b = 2;
[a, b] = [b, a];
let first = 0;
let rest: number[] = [];
[first, ...rest] = [1, 2, 3];
let x = 0, y = 0;
[x = 5, [y]] = [undefined, [7]] as [number | undefined, number[]];
console.log(a, b, first, rest, x, y);
