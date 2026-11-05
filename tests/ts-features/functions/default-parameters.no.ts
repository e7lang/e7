// @feature: Default parameters
// @category: More on Functions
// @status: partial
// @why: not implemented yet: in function declarations, methods and constructors a default cannot refer to an earlier parameter (docs/SPEC.md section 5 says it can; arrow functions and function expressions accept it)
// @instead: an optional parameter and `b ?? a + 1` in the body, or an arrow function
// @error: cannot find name 'a'
function f(a: number, b = a + 1): number {
  return a + b;
}
console.log(f(1), f(1, 5));
