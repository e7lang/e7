// @feature: Rest parameters
// @category: More on Functions
// @status: supported
function multiply(n: number, ...m: number[]): number[] {
  return m.map(x => n * x);
}
const sum = (...xs: number[]): number => xs.reduce((a, b) => a + b, 0);
console.log(multiply(10, 1, 2, 3), sum(), sum(1, 2, 3));
