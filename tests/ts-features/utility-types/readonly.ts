// @feature: `Readonly<T>`
// @category: Utility Types
// @status: unsupported
// @why: types known at compile time: E7 types are concrete; type-level computation is not implemented
// @instead: an interface with `readonly` properties
// @error: utility type 'Readonly' is not supported
interface Todo { title: string }
const todo: Readonly<Todo> = { title: "Delete inactive users" };
console.log(todo.title);
