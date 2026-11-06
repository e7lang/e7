// @error: the return type of an async generator must be AsyncGenerator<T>
async function* g(): Generator<number> {
  yield 1;
}
g();
