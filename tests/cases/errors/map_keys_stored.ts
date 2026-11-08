// @error: is a copy of the elements in E7 (not a live iterator)
const m = new Map<string, number>([["a", 1]]);
const ks = m.keys();
m.set("b", 2);
console.log([...ks]);
