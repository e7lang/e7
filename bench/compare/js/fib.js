const log = typeof print === "function" ? print : console.log;
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
const t0 = Date.now();
log(fib(30), "fib", Date.now() - t0, "ms");
