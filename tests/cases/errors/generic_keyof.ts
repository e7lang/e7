// @error: 'keyof' constraints are not supported
function get<T, K extends keyof T>(o: T, k: K): T[K] {
  return o[k];
}
console.log(get({ a: 1 }, "a"));
