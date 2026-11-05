// @feature: `Partial<T>`
// @category: Utility Types
// @status: unsupported
// @why: types known at compile time: E7 types are concrete; type-level computation is not implemented
// @instead: an interface with optional properties (`interface UserPatch { name?: string; age?: number }`)
// @error: utility type 'Partial' is not supported
interface Todo { title: string; done: boolean }
function update(t: Todo, patch: Partial<Todo>): Todo { return { ...t, ...patch }; }
console.log(update({ title: "a", done: false }, { done: true }));
