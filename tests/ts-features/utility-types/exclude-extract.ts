// @feature: `Exclude<T, U>` and `Extract<T, U>`
// @category: Utility Types
// @status: unsupported
// @why: types known at compile time: union types other than with `null` / `undefined` are not supported
// @instead: write the resulting type
// @error: unknown type 'Exclude'
type T0 = Exclude<"a" | "b" | "c", "a">;
type T1 = Extract<"a" | "b" | "c", "a" | "f">;
const x: T0 = "b";
const y: T1 = "a";
console.log(x, y);
