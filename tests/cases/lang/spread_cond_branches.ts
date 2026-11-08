// { ...(c ? a : b) } copies the properties of the branch taken, also when a branch is not a literal
class Pt { constructor(public x: number, public y: number) {} }
interface Q { a: number; b?: string; x?: number }
function f(c: boolean, d: boolean, q: Q, p: Pt): void {
  const inner = { a: 1 };
  console.log(JSON.stringify({ ...(c ? inner : {}) }));
  console.log(JSON.stringify({ ...(c ? q : p) }));
  console.log(JSON.stringify({ z: 1, ...(c ? p : {}), w: 2 }));
  console.log(JSON.stringify({ x: 0, ...(d ? q : { b: "k" }) }));
  console.log(JSON.stringify({ ...q, ...(c ? { b: "over" } : p) }));
  console.log(JSON.stringify({ ...(c ? (d ? q : p) : { a: 9 }) }));
  console.log(JSON.stringify({ ...(c ? { ...(d ? { a: 1 } : { b: 2 }) } : {}) }));
  const t: Q = { a: 0 };
  Object.assign(t, c ? q : {});
  console.log(JSON.stringify(t));
  const { ...rest } = c ? q : { a: 0 };
  console.log(JSON.stringify(rest));
}
for (const c of [true, false])
  for (const d of [true, false]) {
    f(c, d, { a: 5, b: "s" }, new Pt(1, 2));
    f(c, d, { a: 6, x: 3 }, new Pt(7, 8));
  }
