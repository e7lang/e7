// @feature: Async iteration (`for await`, async generators)
// @category: Iterators and Generators
// @status: partial
// @note: `for await` over arrays of promises or values (each one awaited, in the order of Node.js)
async function sum(xs: Promise<number>[]): Promise<number> {
  let t = 0;
  for await (const x of xs) t += x;
  return t;
}
sum([Promise.resolve(1), Promise.resolve(2)]).then(t => console.log(t));
