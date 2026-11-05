// @feature: `Promise`
// @category: JavaScript: Standard Library
// @status: unsupported
// @why: not implemented yet: no event loop; reserved for coroutines
// @instead: synchronous code; the host does asynchronous work and calls exported functions with the results
// @error: 'Promise' is not supported in E7
Promise.resolve(42).then(v => console.log(v));
