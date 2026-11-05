// @error: has no property 'b'
const t = { a: 1 };
Object.assign(t, { b: 2 });
console.log(t);
