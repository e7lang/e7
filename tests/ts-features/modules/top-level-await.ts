// @feature: Top-level `await`
// @category: Modules
// @status: supported
// @note: in the entry module (an imported module with top-level await is a compile error)
const n = await Promise.resolve(42);
console.log(n);
