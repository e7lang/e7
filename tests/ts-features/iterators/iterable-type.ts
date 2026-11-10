// @feature: `Iterable<T>` and `ArrayLike<T>` types
// @category: Iterators and Generators
// @status: unsupported
// @why: types known at compile time: arrays, Sets, Maps, strings and generators have no common run-time representation, so one parameter cannot take all of them
// @instead: `T[]` (pass a Set or Map as `[...s]`), or `IterableIterator<T>` / `Generator<T>` for a generator
// @error: 'Iterable<T>' is not supported
function sum(xs: Iterable<number>): number {
  let t = 0;
  for (const x of xs) t += x;
  return t;
}
console.log(sum([1, 2, 3]), sum(new Set([4, 5])));
