// @feature: Error types (`Error`, `TypeError`, `RangeError`, `SyntaxError`, `URIError`)
// @category: JavaScript: Standard Library
// @status: partial
// @why: not implemented yet: the `cause` option, `AggregateError`, `stack`
// @instead: put the cause into the message
// @error: options such as { cause } are not supported
const e = new Error("outer", { cause: new Error("inner") });
console.log(e.message);
