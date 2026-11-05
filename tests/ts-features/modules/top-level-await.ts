// @feature: Top-level `await`
// @category: Modules
// @status: unsupported
// @why: not implemented yet: no Promises or coroutines (reserved for a later version)
// @instead: synchronous code; the host does asynchronous work and passes the results in
// @error: 'await' is not supported yet
const n = await Promise.resolve(42);
console.log(n);
