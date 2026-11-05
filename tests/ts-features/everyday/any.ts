// @feature: The `any` type
// @category: Everyday Types
// @status: unsupported
// @why: types known at compile time: every value must have a static type
// @instead: a concrete type or an interface; `Map<K, V>` for dynamic keys
// @error: type 'any' is not supported
function show(x: any): string {
  return String(x);
}
console.log(show(1), show("a"));
