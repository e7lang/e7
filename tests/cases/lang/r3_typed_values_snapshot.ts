// third test round of 0.1.3 (typed_values_snapshot): must print what Node.js prints
const u = new Uint8Array([1, 2]);
for (const v of u.values()) { u[1] = 9; console.log(v); }
const f = new Float64Array([1, 2]);
for (const [i, v] of f.entries()) { f[1] = 9; console.log(i, v); }
const g = new Int32Array([1, 2]);
for (const v of g) { g[1] = 9; console.log(v); }
