// @feature: Async iteration (`for await`, async generators)
// @category: Iterators and Generators
// @status: partial
// @why: not implemented yet: `yield*` of a sync iterable (an array, a generator) in an async generator (its async-from-sync iterator)
// @instead: `for (const x of xs) yield x;`, or `yield*` of an async generator
// @error: yield* in an async generator needs an async iterable
async function* numbers(): AsyncGenerator<number> {
  yield* [1, 2];
}
(async () => {
  for await (const n of numbers()) console.log(n);
})();
