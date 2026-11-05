// @error: does not satisfy the constraint
class Shape {
  area(): number {
    return 1;
  }
}
function total<T extends Shape>(xs: T[]): number {
  return xs.reduce((s, x) => s + x.area(), 0);
}
console.log(total([1, 2]));
