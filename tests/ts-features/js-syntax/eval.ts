// @feature: `eval` and `new Function`
// @category: JavaScript: Syntax
// @status: unsupported
// @why: verifiable bytecode: all code is compiled and verified ahead of time
// @instead: ship the code as a module; the host can compile and load modules
// @error: cannot find name 'eval'
console.log(eval("1 + 1"));
