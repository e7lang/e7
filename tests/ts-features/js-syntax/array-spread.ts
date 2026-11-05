// @feature: Array spread
// @category: JavaScript: Syntax
// @status: supported
const a = [1, 2];
const b = [0, ...a, 3, ...a];
const copy = [...b];
copy.push(9);
console.log(b, copy, [..."hi"]);
