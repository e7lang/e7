// @feature: `Awaited<T>`
// @category: Utility Types
// @status: unsupported
// @why: not implemented yet: no Promises
// @instead: synchronous values
// @error: unknown type 'Awaited'
type A = Awaited<Promise<string>>;
const a: A = "done";
console.log(a);
