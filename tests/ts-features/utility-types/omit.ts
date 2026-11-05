// @feature: `Omit<T, K>`
// @category: Utility Types
// @status: unsupported
// @why: types known at compile time: E7 types are concrete; type-level computation is not implemented
// @instead: a separate interface (the larger interface can extend it)
// @error: utility type 'Omit' is not supported
interface Todo { title: string; description: string; completed: boolean }
const preview: Omit<Todo, "description"> = { title: "Clean room", completed: false };
console.log(preview);
