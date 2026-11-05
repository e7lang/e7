// @feature: `if`, `while`, `do...while`, `for` loops
// @category: JavaScript: Syntax
// @status: supported
let n = 0;
while (n < 3) n++;
let m = 10;
do { m--; } while (m > 7);
const out: number[] = [];
for (let i = 0; i < 10; i += 3) out.push(i);
for (;;) {
  if (out.length > 5) break;
  out.push(-1);
}
if (n === 3 && m === 7) console.log("ok", out);
else console.log("bad");
