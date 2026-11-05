// @feature: Explicit type arguments
// @category: Generics
// @status: supported
function combine<T>(a: T[], b: T[]): T[] {
  return a.concat(b);
}
function make<T>(n: number, v: T): T[] {
  return new Array<T>(n).fill(v);
}
console.log(combine<number>([1], [2, 3]), make<string>(2, "x"));
