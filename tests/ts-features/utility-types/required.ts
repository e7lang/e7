// @feature: `Required<T>`
// @category: Utility Types
// @status: unsupported
// @why: types known at compile time: E7 types are concrete; type-level computation is not implemented
// @instead: spell out the interface without `?`
// @error: utility type 'Required' is not supported
interface Props { a?: number; b?: string }
const obj: Required<Props> = { a: 5, b: "x" };
console.log(obj);
