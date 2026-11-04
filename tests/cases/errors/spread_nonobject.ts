// @error: only objects can be spread
const n = 1;
const o = { ...n, a: 1 };
console.log(o.a);
