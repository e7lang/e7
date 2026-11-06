// @runtime-error: unhandled in generator
// @requires-vm: 0.3.1
// an error thrown by an async generator rejects the promise of next(); nobody handles it, so the
// call fails (Node.js ends the process with it)
async function* g(): AsyncGenerator<number> {
  yield 1;
  throw new Error("unhandled in generator");
}
const it = g();
it.next().then(r => console.log("first", r.value));
it.next();
