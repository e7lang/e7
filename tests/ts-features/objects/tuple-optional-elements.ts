// @feature: Optional tuple elements `[A, B?]`
// @category: Object Types
// @status: unsupported
// @why: types known at compile time: a tuple has one length (its layout is fixed); use `T | undefined` for the element
// @instead: a tuple with a nullable element `[A, B | undefined]`, or an interface with an optional property
// @error: optional tuple elements are not supported
type Either2dOr3d = [number, number, number?];
const p: Either2dOr3d = [1, 2];
console.log(p.length);
