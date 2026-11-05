// @feature: Recursive functions
// @category: More on Functions
// @status: supported
// @note: a recursive function needs an explicit return type (as tsc usually does)
function fib(n: number): number {
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}
function walk(depth: number, acc: string[]): string[] {
  if (depth === 0) return acc;
  acc.push("d" + depth);
  return walk(depth - 1, acc);
}
console.log(fib(20), walk(3, []));
