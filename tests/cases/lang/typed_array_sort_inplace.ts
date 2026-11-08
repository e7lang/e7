// typed arrays: sort(cmp) sorts in place, toSorted() sorts numbers, join prints -0 as 0, for...of is live
const a = Int32Array.of(5, -3, 100);
const r = a.sort((x, y) => y - x);
console.log(a, r === a);
const f = Float64Array.of(10, 9, 100, -0, 1.5);
console.log(f.toSorted(), f.toSorted((x, y) => y - x), f);
console.log(f.join(), String(Float32Array.of(-0, 1)), `${Float64Array.of(-0)}`);
const b = BigInt64Array.of(3n, -1n, 2n);
b.sort((x, y) => (x < y ? -1 : x > y ? 1 : 0));
console.log(b, b.toSorted(), JSON.stringify(new BigInt64Array(0)));
const live = Int32Array.of(1, 2, 3);
for (const x of live) {
  live[2] = 99;
  console.log(x);
}
