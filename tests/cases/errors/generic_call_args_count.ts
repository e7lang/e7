// @error: 'id' expects 1 type argument(s), got 2
function id<T>(x: T): T {
  return x;
}
console.log(id<number, string>(1));
