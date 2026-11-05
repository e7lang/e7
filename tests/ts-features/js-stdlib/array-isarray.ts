// @feature: `Array.isArray`
// @category: JavaScript: Standard Library
// @status: unsupported
// @why: types known at compile time: whether a value is an array is known statically
// @instead: nothing (the static type says it)
// @error: Array.isArray is not needed in E7
const xs: number[] = [1];
console.log(Array.isArray(xs));
