// @feature: Functions as values
// @category: More on Functions
// @status: supported
// @note: function declarations, arrows and function expressions are first-class values; built-in functions (`Math.sqrt`, `parseInt`, `String`, `Boolean`, ...) and functions with default parameters become values where a function type is expected (TypeScript's contextual typing)
function twice(x: number): number { return x * 2; }
function scale(x: number, by = 10): number { return x * by; }
const f = twice;
const table: ((x: number) => number)[] = [twice, x => x + 1, Math.sqrt];
console.log(f(4), [1, 2].map(twice), table.map(g => g(16)), [1, 2].map(scale), ["a", ""].filter(Boolean));
