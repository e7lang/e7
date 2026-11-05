// @feature: `async` functions and `await`
// @category: JavaScript: Syntax
// @status: unsupported
// @why: not implemented yet: reserved for coroutines
// @instead: synchronous functions; the host does asynchronous work (and can run long computations in slices with an instruction budget)
// @error: async functions are not supported yet
async function load(): Promise<string> {
  return "data";
}
async function main(): Promise<void> {
  console.log(await load());
}
main();
