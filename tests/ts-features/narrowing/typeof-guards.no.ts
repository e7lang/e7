// @feature: `typeof` type guards
// @category: Narrowing
// @status: partial
// @why: types known at compile time: narrowing by `typeof` needs a union type or a run-time type tag; in generic code each instantiation is type-checked as a whole, so the branch for other types must also compile
// @instead: separate functions per type, or a field that tells the cases apart
// @error: method 'toFixed' does not exist on type 'string'
function show<T>(x: T): string {
  if (typeof x === "number") return x.toFixed(1);
  return String(x);
}
console.log(show(1), show("a"));
