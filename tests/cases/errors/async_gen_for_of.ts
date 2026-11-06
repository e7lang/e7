// @error: is an async iterable: use 'for await'
async function* g(): AsyncGenerator<number> {
  yield 1;
}
for (const x of g()) console.log(x);
