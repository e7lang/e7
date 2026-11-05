// Inserting, deleting and clearing while iterating a Map / Set visits what JavaScript visits,
// also after the table has been compacted (many deletions) or cleared.
const m = new Map<number, number>();
for (let i = 0; i < 64; i++) m.set(i, i);
const seen: number[] = [];
let added = 0;
for (const [k, v] of m) {
  seen.push(k);
  if (k < 60) m.delete(k + 1);      // many deletions: the next insertion compacts the table
  if (k % 8 === 0 && added < 20) {  // insertions during iteration are visited
    m.set(1000 + added, v);
    added++;
  }
}
console.log(seen.length, seen.slice(0, 12).join(","), seen.slice(-6).join(","));

const c = new Map<string, number>([["a", 1], ["b", 2], ["c", 3]]);
const order: string[] = [];
for (const [k] of c) {
  order.push(k);
  if (k === "b") {
    c.clear();          // the iteration continues with entries added after clear()
    c.set("x", 9);
    c.set("y", 8);
  }
}
console.log(order.join(","), c.size);

const s = new Set<number>([1, 2, 3, 4, 5, 6, 7, 8]);
const got: number[] = [];
s.forEach((x: number): void => {
  got.push(x);
  if (x <= 6) s.delete(x + 1);
  if (x === 1) s.add(100);
  if (x === 100) s.add(2);   // a deleted value added again is visited again
});
console.log(got.join(","));

const r = new Map<number, string>([[1, "a"], [2, "b"], [3, "c"]]);
const rs: string[] = [];
for (const [k, v] of r) {
  rs.push(k + v);
  if (k === 1 && v === "a") { r.delete(1); r.set(1, "again"); }  // re-inserted: moves to the end and is visited again
}
console.log(rs.join(" "));
