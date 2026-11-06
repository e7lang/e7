// @feature: Error types (`Error`, `TypeError`, `RangeError`, `SyntaxError`, `URIError`)
// @category: JavaScript: Standard Library
// @status: partial
// @why: JavaScript-identical behaviour: `stack` (stack traces differ between engines)
// @instead: log `e.message` (the host gets the e7 stack trace of an uncaught error)
// @error: Error.stack is not supported
const e = new Error("x");
console.log(e.stack);
