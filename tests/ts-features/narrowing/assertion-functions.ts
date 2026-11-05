// @feature: Assertion functions (`asserts x is T`)
// @category: Narrowing
// @status: unsupported
// @why: not implemented yet: assertion signatures are not parsed
// @instead: an explicit check that throws, followed by the narrowed use (`if (x === null) throw ...`)
// @error: function overloads / declarations without body are not supported
function assertDefined(x: number | undefined): asserts x is number {
  if (x === undefined) throw new Error("undefined");
}
const n: number | undefined = 1;
assertDefined(n);
console.log(n + 1);
