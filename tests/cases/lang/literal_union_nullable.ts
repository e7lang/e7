// A literal union with null / undefined accepts its literals.
type Op = "eq" | "neq";
interface Cond {
  op?: Op;
  n?: 1 | 2 | null;
}
let x: "a" | "b" | undefined = "a";
console.log(x);
x = undefined;
console.log(x);
const c: Cond = { op: "eq", n: 2 };
console.log(c.op, c.n);
function num(): 1 | 2 | 3 | null {
  return 2;
}
const n = num();
const m: 1 | 2 | null = n === 3 ? null : 1;
console.log(n, m);
let s: Op | null = null;
s = "neq";
const t: Op | null | undefined = s;
console.log(s, t);
function pick(flag: boolean): "x" | "y" | undefined {
  return flag ? "x" : undefined;
}
console.log(pick(true), pick(false));
