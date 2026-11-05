// Maps, sets and arrays with integer-typed keys / values / elements use their own natives (one per
// element kind); no other test called them (0.2.5 coverage report).
const mi = new Map<i32, string>();
mi.set(i32(1), "one");
mi.set(i32(-2), "minus two");
mi.set(i32(3), "three");
console.log(mi.size, mi.has(i32(-2)), mi.has(i32(2)), mi.get(i32(3)), mi.get(i32(4)));
console.log(mi.get(i32(1))!, mi.get(i32(9)) ?? "none", mi.delete(i32(1)), mi.delete(i32(1)), mi.size);
for (const [k, v] of mi) console.log(k, v);
const keys: i32[] = [];
for (const k of mi.keys()) keys.push(k);
console.log(keys.join(","));

const mii = new Map<i32, i32>([[i32(1), i32(10)], [i32(2), i32(20)]]);
mii.set(i32(2), i32(21));
const sum: i32 = mii.get(i32(1))! + mii.get(i32(2))!;
console.log(sum, mii.get(i32(7)) ?? i32(-1));
for (const [k, v] of mii) console.log(k, v * i32(2));
const vals: i32[] = [];
for (const v of mii.values()) vals.push(v);
console.log(vals.join(","));
const min = new Map<i32, number>([[i32(5), 0.5]]);
min.set(i32(6), 2.5);
console.log(min.get(i32(5))!, min.get(i32(6)) ?? 1.5, min.get(i32(7)) ?? 1.5);
const mni = new Map<number, i64>([[1.5, i64(-3)]]);
mni.set(3.5, i64(9));
console.log(mni.get(1.5)!, mni.get(2.5) ?? i64(7), mni.get(3.5)!);
const msi = new Map<string, u32>([["a", u32(4000000000)]]);
msi.set("c", u32(5));
console.log(msi.get("a")!, msi.get("b") ?? u32(0), msi.get("c")!);
const mir = new Map<i32, i32[]>([[i32(0), [i32(1), i32(2)]]]);
console.log(mir.get(i32(0))!.length);

const si = new Set<u32>();
si.add(u32(7));
si.add(u32(7));
si.add(u32(8));
console.log(si.size, si.has(u32(7)), si.has(u32(9)), si.delete(u32(7)), si.size);

const a: i32[] = [i32(5), i32(6), i32(7), i32(6)];
console.log(a.includes(i32(6)), a.includes(i32(9)), a.indexOf(i32(6)), a.indexOf(i32(6), 2), a.lastIndexOf(i32(6)),
  a.lastIndexOf(i32(5), -2));
console.log(a.unshift(i32(4)), a.join(","), a.fill(i32(0), 1, 3).join(","), a.fill(i32(-1)).join(","));
const b: i64[] = [i64(1), i64(2), i64(3)];
const last: i64 = b.pop()!;
const first: i64 = b.shift()!;
console.log(last, first, b.length);

console.log(BigInt(i64(-5)) * 3n, BigInt(i64(123456789)) + 1n);
console.log(idiv(7, 2), idiv(-7, 2), imod(7, 3), imod(-7, 3), idiv(1, 0), imod(5.5, 2));
