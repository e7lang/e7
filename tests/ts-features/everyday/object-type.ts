// @feature: The `object` type
// @category: Everyday Types
// @status: unsupported
// @why: types known at compile time: `object` has no known properties
// @instead: an interface or a class
// @error: type 'object' is not supported
function keysOf(o: object): string[] {
  return Object.keys(o);
}
console.log(keysOf({ a: 1 }));
