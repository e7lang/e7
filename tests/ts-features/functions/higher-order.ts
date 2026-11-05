// @feature: Higher-order functions and callbacks with fewer parameters
// @category: More on Functions
// @status: supported
function compose(f: (x: number) => number, g: (x: number) => number): (x: number) => number {
  return x => g(f(x));
}
const inc = (x: number) => x + 1;
const dbl = (x: number) => x * 2;
console.log(compose(inc, dbl)(5), ["a", "b"].map((s, i) => s + i), [3, 4].map(() => 0));
