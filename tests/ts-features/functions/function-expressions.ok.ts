// @feature: Function expressions
// @category: More on Functions
// @status: partial
// @note: anonymous function expressions
const square = function (x: number): number { return x * x; };
const apply = function (f: (x: number) => number, x: number): number { return f(x); };
console.log(square(7), apply(function (x: number): number { return x + 1; }, 1));
