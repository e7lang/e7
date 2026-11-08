// @error: do not get the typed array as a parameter
const t = Int32Array.of(1, 2, 3);
t.forEach((x, i, arr) => { arr[i] = x * 10; });
console.log(t);
