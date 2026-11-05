// @feature: Iterating `Map` and `Set`
// @category: Iterators and Generators
// @status: supported
const m = new Map<string, number>([["one", 1], ["two", 2]]);
const s = new Set<string>(["x", "y", "x"]);
for (const [k, v] of m) console.log(k, v);
for (const v of s) console.log(v);
for (const k of m.keys()) console.log(k);
console.log([...m.values()], [...m.entries()], [...s]);
m.forEach((v, k) => console.log(k, "=>", v));
