const log = typeof print === "function" ? print : console.log;
const t0 = Date.now();
const N = 5000000;
const flags = new Array(N + 1).fill(true);
let count = 0;
for (let i = 2; i <= N; i++) {
  if (flags[i]) { count++; for (let j = i * 2; j <= N; j += i) flags[j] = false; }
}
log(count, "sieve", Date.now() - t0, "ms");
