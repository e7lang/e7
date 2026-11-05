// @feature: The `arguments` object
// @category: More on Functions
// @status: unsupported
// @why: types known at compile time: `arguments` is untyped
// @instead: rest parameters `...args: T[]`
// @error: cannot find name 'arguments'
function count(): number {
  return arguments.length;
}
console.log(count());
