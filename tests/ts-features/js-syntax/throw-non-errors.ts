// @feature: Throwing values other than `Error` objects
// @category: JavaScript: Syntax
// @status: unsupported
// @why: types known at compile time: a caught value must have a known type
// @instead: `throw new Error(message)` (or `TypeError`, `RangeError`, ...)
// @error: throwing a string is not supported
try {
  throw "failed";
} catch (e) {
  console.log(e);
}
