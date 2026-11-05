// Math.pow and ** are V8's fdlibm pow, as in Node.js 22: e.g. 10 ** -5 is 0.000009999999999999999.
// The C library's pow (what e7 used before; macOS libm) gives a different last bit in most of these
// cases, and Node.js 23+ uses the C library's pow by default (V8 flag --use-std-math-pow), which is why
// this test compares with math_pow_fdlibm.out: the output of Node.js 22, which is also the output of
// `node --no-use-std-math-pow` with Node.js 24 (the same fdlibm code, V8's base::ieee754::legacy::pow).
function show(x: number): string {
  return x === 0 && 1 / x < 0 ? "-0" : String(x);
}
const CASES: number[][] = [[10, -5], [10, -4], [10, -7], [10, -8], [2, -0.5], [0.5, 0.5], [0.5, -0.5], [0.5, 1.5],
  [0.5, -2.5], [0.1, -8.25], [0.1, -6.75], [0.1, -3.75], [0.1, 2.75], [0.1, 3.75], [0.1, 5.5], [0.2, -7.5],
  [0.2, -5.75], [0.2, -2.25], [0.2, 4.5], [0.3, -8.25], [0.3, -3.75], [1.1, 10.5], [3, 0.1], [7, 1 / 3]];
for (const c of CASES) console.log(`${c[0]} ** ${show(c[1])} =`, show(Math.pow(c[0], c[1])), show(c[0] ** c[1]));
