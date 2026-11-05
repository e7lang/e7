// @error: cannot infer the type argument 'T' of 'make'
function make<T>(): T[] {
  return [];
}
console.log(make().length);
