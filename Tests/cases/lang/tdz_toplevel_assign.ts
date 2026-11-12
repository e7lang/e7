// Top-level let / const: assignments after the declaration (plain, compound, in loops, destructuring)
// do not need the "declaration has run" flag again; functions called before a declaration still throw.
interface P { x: number; y: number; q: { z: number } }
function mk(): P { return { x: 1, y: 2, q: { z: 3 } }; }
function early(): string {
  try { return String(a1 + b1 + x1 + z1 + r1.length + d1 + u1 + e1 + w1 + n1); } catch (e) { return "TDZ: " + (e as Error).message; }
}
console.log(early());
const [a1, b1] = [10, 20];
console.log(early());
const { x: x1, q: { z: z1 } } = mk();
console.log(early());
const [h1, ...r1] = [1, 2, 3];
const [d1 = 7] = [] as number[];
let u1: number;
u1 = 5;
console.log(early());
let e1 = 0;
for (let i = 0; i < 3; i++) { e1 += i; }
let w1 = 0;
[w1] = [42];
let n1 = 0;
({ x: n1 } = mk());
console.log(early(), h1);
function late(): number { return k1 + m1; }
try { console.log(late()); } catch (e) { console.log("TDZ: " + (e as Error).message); }
let k1 = 1, m1 = k1 + 1;
console.log(late());
let lateG = 100; for (const v of [1, 2]) { const g = (): number => v + lateG; console.log(g()); }
function useG(): number { return lateG; }
var_like();
function var_like(): void { try { console.log(useG()); } catch (e) { console.log("TDZ: " + (e as Error).message); } }
