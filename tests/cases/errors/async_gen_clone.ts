// @error: an async generator
async function* g(): AsyncGenerator<number> {
  yield 1;
}
const c = structuredClone({ it: g() });
console.log(c);
