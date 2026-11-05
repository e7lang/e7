// @feature: Async iteration (`for await`, async generators)
// @category: Iterators and Generators
// @status: unsupported
// @why: not implemented yet: no Promises or coroutines
// @instead: synchronous iteration over the data the host passes in
// @error: async functions are not supported yet
async function sum(xs: number[]): Promise<number> {
  let t = 0;
  for await (const x of xs) t += x;
  return t;
}
sum([1, 2]).then(t => console.log(t));
