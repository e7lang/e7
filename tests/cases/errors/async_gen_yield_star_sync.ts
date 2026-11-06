// @error: yield* in an async generator needs an async iterable
// (arrays, generators and iterable classes are accepted; a Set is not)
async function* g(): AsyncGenerator<number> {
  yield* new Set<number>([1, 2, 3]);
}
g();
