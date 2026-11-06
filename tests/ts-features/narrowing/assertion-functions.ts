// @feature: Assertion functions (`asserts x is T`)
// @category: Narrowing
// @status: supported
// @note: `asserts x is T` and `asserts x`: after the call, the rest of the block sees the narrowed type (for nullable values, literal types and classes)
function assertDefined(x: number | undefined): asserts x is number {
  if (x === undefined) throw new Error("undefined");
}
function check(cond: boolean): asserts cond {
  if (!cond) throw new Error("check failed");
}
const n: number | undefined = 1;
assertDefined(n);
const s: string | null = "a";
check(s !== null);
console.log(n + 1, s.length);
