// @error: an object destructuring assignment must be a statement of its own
let a = 0;
const r = ({ a } = { a: 1 });
console.log(a, r);
