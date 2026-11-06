// @feature: Optional tuple elements `[A, B?]`
// @category: Object Types
// @status: unsupported
// @why: not implemented yet: tuples have a fixed length
// @instead: a tuple with a nullable element `[A, B | undefined]`, or an interface with an optional property
// @error: optional tuple elements are not supported
type Either2dOr3d = [number, number, number?];
const p: Either2dOr3d = [1, 2];
console.log(p.length);
