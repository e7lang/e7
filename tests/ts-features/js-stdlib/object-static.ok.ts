// @feature: `Object` static methods
// @category: JavaScript: Standard Library
// @status: partial
// @note: `Object.keys`, `values`, `entries`, `assign`, `freeze`
interface Scores { math: number; art: number; music?: number }
const s: Scores = { math: 90, art: 75 };
console.log(Object.keys(s), Object.values(s), Object.entries(s));
const target = { a: 1, b: 2 };
Object.assign(target, { b: 3 });
console.log(target, Object.freeze(target) === target, Object.assign({ x: 1 }, { y: 2 }));
