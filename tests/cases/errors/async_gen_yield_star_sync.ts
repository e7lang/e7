// @error: yield* in an async generator needs an async iterable
async function* g(): AsyncGenerator<number> {
  yield* [1, 2, 3];
}
g();
