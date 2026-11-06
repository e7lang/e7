// @feature: Generator functions (`function*`, `yield`, `yield*`)
// @category: Iterators and Generators
// @status: supported
// @note: `next()` on a finished generator whose return type is a primitive gives NaN / 0 instead of undefined (the value's representation is fixed)
function* range(n: number): Generator<number> {
  for (let i = 0; i < n; i++) yield i;
}
function* twice(n: number): Generator<number> {
  yield* range(n);
  yield* range(n);
}
const g = range(2);
console.log([...range(3)], [...twice(2)], g.next(), g.next(), g.next());
