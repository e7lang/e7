// @feature: `NonNullable<T>`
// @category: Utility Types
// @status: unsupported
// @why: not implemented yet
// @instead: the type without `| null | undefined`
// @error: unknown type 'NonNullable'
type T = NonNullable<string | null | undefined>;
const s: T = "x";
console.log(s);
