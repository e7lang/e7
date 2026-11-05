// @feature: The `unknown` type
// @category: Everyday Types
// @status: partial
// @why: types known at compile time: an `unknown` value would need run-time type tests
// @instead: a concrete type; `JSON.parse(text) as T` / `tryParse` for external data
// @error: type 'unknown' is not supported
const value: unknown = JSON.parse("42");
if (typeof value === "number") console.log(value + 1);
