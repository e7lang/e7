// @error: generic methods in object literals are not supported
const o = {
  wrap<T>(x: T): T[] {
    return [x];
  },
};
console.log(o.wrap(1));
