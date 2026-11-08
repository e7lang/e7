// A conditional of object literals is one type in which a property of only one branch is optional
// (TypeScript's normalization), so a spread or a read of it keeps that property.
function mk(c: boolean, d: boolean): void {
  const o = { a: 1, ...(c ? { badge: "VIP" } : {}) };
  console.log(JSON.stringify(o));
  const x = c ? { badge: "VIP", n: 2 } : { n: 3, m: true };
  console.log(JSON.stringify(x), x.n, x.badge, x.m);
  const p = { ...(c ? { b: "x" } : {}) };
  console.log(JSON.stringify(p));
  const q = { z: 0, ...(c ? { k: 1 } : d ? { k: 2, j: "j" } : {}) };
  console.log(JSON.stringify(q), q.k, q.j);
  const arr = [c ? { v: 1.5 } : {}, { v: 2 }];
  console.log(JSON.stringify(arr));
}
mk(true, false);
mk(false, true);
mk(false, false);

// spreading empty objects
const e = { ...{} };
console.log(JSON.stringify(e));
let n = 0;
function side(): {} {
  n++;
  return {};
}
console.log(JSON.stringify({ ...side(), ...side() }), n);
