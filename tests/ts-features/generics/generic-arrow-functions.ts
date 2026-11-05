// @feature: Generic arrow functions
// @category: Generics
// @status: supported
// @note: must be assigned to a `const`
const pair = <A, B>(a: A, b: B): [A, B] => [a, b];
const last = <T>(xs: T[]): T => xs[xs.length - 1];
console.log(pair(1, "x"), last([1, 2, 3]), last(["a"]));
