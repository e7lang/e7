// @error: would be a union of 'number' and 'string'
async function* g(): AsyncGenerator<number, string> {
  yield 1;
  return "done";
}
g().next().then(r => console.log(r.value));
