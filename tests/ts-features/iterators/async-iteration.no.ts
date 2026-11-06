// @feature: Async iteration (`for await`, async generators)
// @category: Iterators and Generators
// @status: partial
// @why: not implemented yet: async generators (`async function*`)
// @instead: an async function that returns an array, or a generator of promises
// @error: async generators are not supported yet
async function* numbers(): AsyncGenerator<number> {
  yield 1;
  yield 2;
}
(async () => {
  for await (const n of numbers()) console.log(n);
})();
