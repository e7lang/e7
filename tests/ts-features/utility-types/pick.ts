// @feature: `Pick<T, K>`
// @category: Utility Types
// @status: unsupported
// @why: types known at compile time: E7 types are concrete; type-level computation is not implemented
// @instead: a separate interface (the source interface can extend it)
// @error: utility type 'Pick' is not supported
interface Todo { title: string; description: string; completed: boolean }
const preview: Pick<Todo, "title" | "completed"> = { title: "Clean room", completed: false };
console.log(preview);
