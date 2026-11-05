// @feature: `Object` static methods
// @category: JavaScript: Standard Library
// @status: partial
// @why: verifiable bytecode: methods that create, inspect or change object layouts and prototypes at run time (`create`, `defineProperty`, `getPrototypeOf`, `setPrototypeOf`, `fromEntries`, `groupBy`)
// @instead: classes, object literals and `Map`
// @error: 'Object.fromEntries' is not supported
const o = Object.fromEntries([["a", 1], ["b", 2]]);
console.log(o, Object.getPrototypeOf(o) === Object.prototype);
