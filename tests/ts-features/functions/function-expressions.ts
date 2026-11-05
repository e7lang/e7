// @feature: Function expressions
// @category: More on Functions
// @status: supported
// @note: a named function expression's name is in scope in its body (recursion)
const square = function (x: number): number { return x * x; };
const apply = function (f: (x: number) => number, x: number): number { return f(x); };
const fact = function f(n: number): number { return n <= 1 ? 1 : n * f(n - 1); };
console.log(square(7), apply(function (x: number): number { return x + 1; }, 1), fact(10));
