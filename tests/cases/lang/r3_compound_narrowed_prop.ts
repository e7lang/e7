// third test round of 0.1.3 (compound_narrowed_prop): must print what Node.js prints
interface I { u?: number }
const o: I = {};
let x: number | undefined;
x = 1;
x += 1;
o.u = 1;
o.u += 1;
o.u++;
console.log(o, x);
