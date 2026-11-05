// @runtime-error: does not declare: b
// a property that the static type does not declare has an unknown type: an e7 error
const full = { a: 1, b: 2 };
const part: { a: number } = full;
console.log(Object.values(part));
