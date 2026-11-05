// @feature: Destructuring assignment to existing variables
// @category: JavaScript: Syntax
// @status: partial
// @why: not implemented yet: defaults and rest elements in assignment patterns
// @instead: a declaration: `const [first, ...rest] = xs`
// @error: rest elements in destructuring assignments are not supported
let first = 0;
let rest: number[] = [];
[first, ...rest] = [1, 2, 3];
console.log(first, rest);
