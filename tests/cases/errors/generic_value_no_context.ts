// @error: needs a contextual function type to be used as a value
function id<T>(x: T): T {
  return x;
}
const f = id;
console.log(f(1));
