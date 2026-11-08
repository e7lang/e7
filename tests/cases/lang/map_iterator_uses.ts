// map.keys() / values() / entries() where they are read at once
const m = new Map<string, number>([["a", 1], ["b", 2]]);
const s = new Set<number>([3, 4]);
console.log([...m.keys()], Array.from(m.values()), new Set(m.keys()).size, [...s.values()], Math.max(...m.values()));
for (const [k, v] of m.entries()) console.log(k, v);
const copy = new Map(m.entries());
console.log(copy.size);
