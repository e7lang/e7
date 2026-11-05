// @feature: `typeof` type guards
// @category: Narrowing
// @status: partial
// @note: `typeof x` is a compile-time constant: the static type name, also in each instantiation of a generic function
function kind<T>(x: T): string {
  return typeof x;
}
const n = 3;
console.log(typeof n, typeof "s", typeof 1n, typeof [1], kind(true), kind("a"), kind(2));
