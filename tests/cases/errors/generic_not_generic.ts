// @error: 'twice' is not generic
function twice(x: number): number {
  return 2 * x;
}
console.log(twice<number>(2));
