// @feature: Spread arguments (arrays into rest parameters, tuples into parameters)
// @category: More on Functions
// @status: supported
function sum(...xs: number[]): number { return xs.reduce((a, b) => a + b, 0); }
function point(x: number, y: number): string { return `(${x}, ${y})`; }
const arr = [1, 2, 3];
const pair: [number, number] = [8, 5];
console.log(sum(...arr, 4, ...arr), point(...pair), Math.max(...arr));
