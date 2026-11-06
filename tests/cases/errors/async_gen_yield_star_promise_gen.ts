// @error: a generator of promises must return void
function* ps(): Generator<Promise<number>, Promise<number>> {
  yield Promise.resolve(1);
  return Promise.resolve(2);
}
async function* g(): AsyncGenerator<number> {
  yield* ps();
}
g();
