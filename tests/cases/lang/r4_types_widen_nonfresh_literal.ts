// fourth test round of 0.1.3 (types/widen_nonfresh_literal): must print what Node.js prints
type Dir = "n" | "s";
function g(r: Dir): string { return r.toUpperCase(); }
function f(d: Dir): string {
  const o = { d };        // tsc: { d: Dir } (d is not a fresh literal)
  const a = [d];          // tsc: Dir[]
  let r = d;              // tsc: Dir
  return g(o.d) + g(a[0]) + g(r);
}
console.log(f("s"));
