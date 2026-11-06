// @feature: Conditional types and `infer`
// @category: Type Manipulation
// @status: unsupported
// @why: types known at compile time: E7 types are concrete; type-level computation is not implemented
// @instead: write the resulting types out (one per case)
// @error: conditional types (T extends U ? X : Y) are not supported
type Flatten<T> = T extends unknown[] ? T[number] : T;
type ElementType<T> = T extends (infer U)[] ? U : never;
const s: Flatten<string[]> = "a";
const n: ElementType<number[]> = 1;
console.log(s, n);
