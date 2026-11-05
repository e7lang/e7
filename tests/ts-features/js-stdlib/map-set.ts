// @feature: `Map` and `Set`
// @category: JavaScript: Standard Library
// @status: supported
const m = new Map<string, number>();
m.set("a", 1).set("b", 2);
m.set("a", 3);
console.log(m.get("a"), m.get("z"), m.has("b"), m.size, m.delete("b"), m);
const s = new Set<number>([1, 2, 3]);
s.add(2).add(4);
s.delete(1);
console.log(s.has(2), s.size, s, new Map(m), [...new Set([3, 3, 1])]);
const byLen = new Map<number, string[]>();
for (const w of ["a", "bb", "cc", "d"]) {
  const list = byLen.get(w.length) ?? [];
  list.push(w);
  byLen.set(w.length, list);
}
console.log(byLen);
m.clear();
console.log(m.size);
