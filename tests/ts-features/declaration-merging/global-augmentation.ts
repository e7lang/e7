// @feature: Global augmentation (`declare global`)
// @category: Declaration Merging
// @status: unsupported
// @why: verifiable bytecode: built-in types and globals are fixed
// @instead: a module that exports the extra functions
// @error: only 'declare function' is supported
declare global {
  interface Array<T> { second(): T | undefined }
}
console.log([1, 2].length);
