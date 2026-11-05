// @feature: Generator functions (`function*`, `yield`, `yield*`)
// @category: Iterators and Generators
// @status: unsupported
// @why: not implemented yet: reserved for coroutines
// @instead: return an array, or a class with a `next()` method
// @error: generator functions are not supported yet
function* range(n: number): Generator<number> {
  for (let i = 0; i < n; i++) yield i;
}
console.log([...range(3)]);
