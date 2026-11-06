// @feature: `NonNullable<T>`
// @category: Utility Types
// @status: supported
// @note: `NonNullable<T>` removes `null` and `undefined`
type T = NonNullable<string | null | undefined>;
const s: T = "x";
console.log(s);
