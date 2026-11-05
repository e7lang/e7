// @feature: Parameter destructuring
// @category: More on Functions
// @status: supported
interface ABC { a: number; b: number; c?: number }
function sum({ a, b, c = 0 }: ABC): number {
  return a + b + c;
}
function head([first, ...rest]: number[]): string {
  return `${first} then ${rest.length}`;
}
console.log(sum({ a: 10, b: 3 }), sum({ a: 1, b: 2, c: 3 }), head([5, 6, 7]));
