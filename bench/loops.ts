const t0 = Date.now();
let s = 0;
for (let i = 0; i < 30000000; i++) { s += i % 7; }
console.log(s, "loop", Date.now() - t0, "ms");
