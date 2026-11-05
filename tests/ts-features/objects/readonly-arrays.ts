// @feature: `readonly T[]` and `ReadonlyArray<T>`
// @category: Object Types
// @status: supported
// @note: `ReadonlyMap` and `ReadonlySet` too; mutating methods, index and `length` writes are compile errors
function total(xs: readonly number[]): number {
  return xs.reduce((a, b) => a + b, 0);
}
const ro: ReadonlyArray<string> = ["a", "b"];
console.log(total([1, 2, 3]), ro.join("-"), ro.length);
