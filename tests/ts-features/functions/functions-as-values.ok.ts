// @feature: Functions as values
// @category: More on Functions
// @status: partial
// @note: function declarations, arrows and function expressions are first-class values
function twice(x: number): number { return x * 2; }
const f = twice;
const table: ((x: number) => number)[] = [twice, x => x + 1, x => Math.sqrt(x)];
console.log(f(4), [1, 2].map(twice), table.map(g => g(16)));
