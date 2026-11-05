// @feature: The comma operator
// @category: JavaScript: Syntax
// @status: unsupported
// @why: not implemented yet
// @instead: separate statements (also for several updates in a `for` header)
// @error: the comma operator is not supported
let a = 1;
const b = (a++, a * 10);
console.log(b);
