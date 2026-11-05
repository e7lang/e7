// @feature: Functions as values
// @category: More on Functions
// @status: partial
// @why: not implemented yet: function declarations with default parameters, and built-in functions such as `Math.sqrt`, have no closure form
// @instead: wrap it in an arrow function: `xs.map(x => scale(x))`, `xs.map(x => Math.sqrt(x))`
// @error: functions with default parameters cannot be used as values
function scale(x: number, by = 10): number { return x * by; }
console.log([1, 2].map(scale));
