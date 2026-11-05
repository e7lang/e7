// @feature: Reverse mappings and enums as run-time objects (`E[value]`, `Object.keys(E)`)
// @category: Enums
// @status: unsupported
// @why: types known at compile time: enum members are compile-time constants, there is no enum object
// @instead: a `switch` or a `Map<E, string>` from values to names
// @error: cannot be used as a value
enum Color { Red, Green, Blue }
console.log(Color[Color.Green], Object.keys(Color));
