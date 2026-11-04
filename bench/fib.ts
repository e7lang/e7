function fib(n: number): number { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
const t0 = Date.now();
console.log(fib(30), "fib", Date.now() - t0, "ms");
