// @feature: The `delete` operator
// @category: JavaScript: Syntax
// @status: unsupported
// @why: types known at compile time: object layouts are fixed
// @instead: an optional property set to `undefined`, or `Map.delete`
// @error: 'delete' is not supported
const o: { a?: number; b: number } = { a: 1, b: 2 };
delete o.a;
console.log(o);
