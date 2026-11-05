// @feature: `never` return type
// @category: More on Functions
// @status: supported
function fail(msg: string): never {
  throw new Error(msg);
}
function check(n: number): number {
  if (n < 0) fail("negative");
  return Math.sqrt(n);
}
console.log(check(16));
try {
  check(-1);
} catch (e) {
  if (e instanceof Error) console.log("caught", e.message);
}
