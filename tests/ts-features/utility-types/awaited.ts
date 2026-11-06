// @feature: `Awaited<T>`
// @category: Utility Types
// @status: supported
// @note: `Awaited<Promise<T>>` is `T`
type A = Awaited<Promise<string>>;
const a: A = "done";
console.log(a);
