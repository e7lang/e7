// @feature: Getters and setters in object literals
// @category: Object Types
// @status: unsupported
// @why: not implemented yet: object literal properties are plain fields or methods
// @instead: a class with `get` / `set` accessors, or methods
// @error: accessor / async properties are not supported in object literals
const temp = {
  celsius: 20,
  get fahrenheit(): number { return this.celsius * 1.8 + 32; },
};
console.log(temp.fahrenheit);
