// @feature: Function expressions
// @category: More on Functions
// @status: partial
// @why: not implemented yet: the name of a named function expression is not in scope inside its body
// @instead: a function declaration, or recursion through the `const` it is assigned to
// @error: cannot find name 'f'
const fact = function f(n: number): number { return n <= 1 ? 1 : n * f(n - 1); };
console.log(fact(10));
