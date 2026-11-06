// @error: cannot be spread
async function* g(): AsyncGenerator<number> {
  yield 1;
}
const xs = [...g()];
console.log(xs);
