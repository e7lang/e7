// @feature: `globalThis` and global object properties
// @category: JavaScript: Standard Library
// @status: unsupported
// @why: verifiable bytecode: there is no global object; names are resolved at compile time
// @instead: module-level variables
// @error: 'globalThis' cannot be used as a value
console.log(typeof globalThis);
