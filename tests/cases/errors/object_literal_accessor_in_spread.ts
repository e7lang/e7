// @error: accessors in an object literal with spread properties are not supported
const base = { a: 1 };
const o = {
  ...base,
  get b(): number {
    return 2;
  },
};
console.log(o.b);
