// @feature: Union types
// @category: Everyday Types
// @status: partial
// @why: types known at compile time: a value of `string | number` needs a run-time type tag that typed registers do not carry
// @instead: one function per type, an interface with a discriminating field and optional properties, or overload by name
// @error: union types other than 'T | null | undefined' are not supported
function printId(id: number | string): void {
  console.log("Your ID is: " + id);
}
printId(101);
printId("202");
