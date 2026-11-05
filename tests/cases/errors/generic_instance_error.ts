// @error: (in double<string>)
function double<T>(x: T): number {
  return (x as number) * 2;
}
console.log(double(2), double("s"));
