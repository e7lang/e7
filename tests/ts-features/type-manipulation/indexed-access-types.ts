// @feature: Indexed access types `T["key"]`
// @category: Type Manipulation
// @status: unsupported
// @why: types known at compile time: types derived from other types (indexed access) are excluded (SPEC 0.1); write the type out
// @instead: declare the property type separately and refer to it by name
// @error: indexed access types (T["key"]) are not supported
interface Person { age: number; name: string; alive: boolean }
type Age = Person["age"];
const a: Age = 30;
console.log(a);
