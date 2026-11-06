// @feature: Rest elements in tuple types `[A, ...B[]]`
// @category: Object Types
// @status: unsupported
// @why: types known at compile time: a tuple has one length (its layout is fixed); use an array
// @instead: an interface `{ head: A; rest: B[] }`
// @error: rest elements in tuple types are not supported
type StringNumberBooleans = [string, number, ...boolean[]];
const a: StringNumberBooleans = ["hello", 1, true, false];
console.log(a.length);
