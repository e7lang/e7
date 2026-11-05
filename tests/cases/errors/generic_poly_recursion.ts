// @error: cannot be compiled by monomorphization
// a generic that instantiates itself with ever larger type arguments
function nest<T>(x: T, n: number): number {
  return n === 0 ? 0 : nest<T[]>([x], n - 1) + 1;
}
console.log(nest(1, 3));
