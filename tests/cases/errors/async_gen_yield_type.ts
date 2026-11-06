// @error: is not assignable to type 'number'
async function* g(): AsyncGenerator<number> {
  yield 1;
  yield Promise.resolve("two");
}
g();
