// @feature: `JSON.stringify` / `JSON.parse`
// @category: JavaScript: Standard Library
// @status: partial
// @why: types known at compile time: `JSON.parse` returns `any`; reviver functions see untyped values
// @instead: `JSON.parse(text) as T` (or `tryParse(text) as T | null` from `e7:json`)
// @error: JSON.parse must be used with a type assertion
const data = JSON.parse('{"a":1}');
console.log(data.a);
