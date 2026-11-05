// @feature: Spreading iterables and `Array.from`
// @category: Iterators and Generators
// @status: supported
const s = new Set([3, 1, 3, 2]);
const m = new Map<number, string>([[1, "a"], [2, "b"]]);
function total(...xs: number[]): number { return xs.reduce((a, b) => a + b, 0); }
console.log([...s], Array.from(s).sort(), total(...s), [...m.keys()], [...m], Array.from("abc"), [..."héllo"]);
