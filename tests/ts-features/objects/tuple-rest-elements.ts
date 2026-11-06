// @feature: Rest elements in tuple types `[A, ...B[]]`
// @category: Object Types
// @status: unsupported
// @why: not implemented yet: tuples have a fixed length
// @instead: an interface `{ head: A; rest: B[] }`
// @error: rest elements in tuple types are not supported
type StringNumberBooleans = [string, number, ...boolean[]];
const a: StringNumberBooleans = ["hello", 1, true, false];
console.log(a.length);
