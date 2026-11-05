// @error: a spread argument must either have a tuple type or be passed to a rest parameter
function add(a: number, b: number): number {
  return a + b;
}
const xs = [1, 2];
console.log(add(...xs));
