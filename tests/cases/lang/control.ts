function classify(n: number): string {
  if (n < 0) return "negative";
  else if (n === 0) return "zero";
  else if (n < 10) return "small";
  return "large";
}
for (const n of [-5, 0, 3, 42]) console.log(n, classify(n));
let total = 0;
for (let i = 0; i < 100; i++) {
  if (i % 2 === 0) continue;
  if (i > 50) break;
  total += i;
}
console.log("total", total);
let k = 0;
while (true) {
  k++;
  if (k >= 7) break;
}
console.log("k", k);
let d = 0;
do { d += 3; } while (d < 10);
console.log("d", d);
outer: for (let i = 0; i < 5; i++) {
  for (let j = 0; j < 5; j++) {
    if (j === 3) continue outer;
    if (i === 3) break outer;
    console.log(i, j);
  }
}
function sw(x: number): string {
  let r = "";
  switch (x) {
    case 1: r += "one;";
    case 2: r += "two;"; break;
    case 3: { r += "three;"; break; }
    default: r += "other;";
  }
  return r;
}
console.log(sw(1), sw(2), sw(3), sw(9));
function sws(s: string): number {
  switch (s) {
    case "a": return 1;
    case "b": return 2;
  }
  return 0;
}
console.log(sws("a"), sws("b"), sws("zz"));
const c = 5 > 3 ? (2 > 1 ? "yes" : "no") : "never";
console.log(c);
let n = 0;
for (;;) { if (++n > 3) break; }
console.log(n);
