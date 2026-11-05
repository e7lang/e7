// Math functions and ** return exactly the doubles of Node.js (V8's fdlibm), not the C library's:
// every value below differed in the last bit with macOS libm before e7 used its fdlibm port
// (third_party/fdlibm). The arguments are ones whose results do not depend on FMA contraction, so
// they also match Node.js builds that contract fdlibm (macOS / Linux arm64).
function show(x: number): string {
  return x === 0 && 1 / x < 0 ? "-0" : String(x);
}
function row(name: string, xs: number[], f: (x: number) => number): void {
  console.log(name, xs.map((x: number) => show(f(x))).join(" "));
}

const SPECIAL: number[] = [0, -0, Infinity, -Infinity, NaN, 5e-324, -5e-324, 2.2250738585072014e-308, 1.7976931348623157e308];
row("acos", [-0.94, -0.86, -0.84, 1, -1, 1.0000000000000002, ...SPECIAL], (x: number) => Math.acos(x));
row("acosh", [1.06, 1.1, 1.15, 1, 0.9999999999999999, ...SPECIAL], (x: number) => Math.acosh(x));
row("asin", [-0.96, -0.58, -0.5, 1, -1, ...SPECIAL], (x: number) => Math.asin(x));
row("asinh", [-29.45, -29.31, -27.92, 1e300, ...SPECIAL], (x: number) => Math.asinh(x));
row("atan", [-28.86, -28.72, -28.64, 1e300, ...SPECIAL], (x: number) => Math.atan(x));
row("atanh", [-0.7, -0.68, -0.5, 1, -1, ...SPECIAL], (x: number) => Math.atanh(x));
row("cbrt", [-30, -29.95, -29.9, 27, -8, ...SPECIAL], (x: number) => Math.cbrt(x));
row("cos", [-29.1, -29.04, -28.95, ...SPECIAL], (x: number) => Math.cos(x));
row("cosh", [-29.58, -29.49, -29.48, 710.4758600739439, 710.475860073944, ...SPECIAL], (x: number) => Math.cosh(x));
row("exp", [-29.96, -29.89, -29.84, 709.782712893384, 709.7827128933841, -745.1332191019411, -745.1332191019412, ...SPECIAL], (x: number) => Math.exp(x));
row("expm1", [1, -4.29, -4.01, -2.93, 709.782712893384, ...SPECIAL], (x: number) => Math.expm1(x));
row("log", [0.09, 0.18, 0.37, 1, -1, ...SPECIAL], (x: number) => Math.log(x));
row("log1p", [-0.99, -0.83, -0.67, -1, -1.0000000000000002, ...SPECIAL], (x: number) => Math.log1p(x));
row("log2", [1.06, 1.3, 1.36, 1024, 0.125, ...SPECIAL], (x: number) => Math.log2(x));
row("log10", [0.52, 0.54, 0.58, 1000, 1e-300, ...SPECIAL], (x: number) => Math.log10(x));
row("sin", [-29.5, -29.47, -29.21, ...SPECIAL], (x: number) => Math.sin(x));
row("sinh", [-29.58, -29.49, -29.48, 710.4758600739439, ...SPECIAL], (x: number) => Math.sinh(x));
row("tan", [-30, -29.98, -29.97, ...SPECIAL], (x: number) => Math.tan(x));
row("tanh", [-2.76, -2.23, -1.84, 22, ...SPECIAL], (x: number) => Math.tanh(x));

// trigonometric argument reduction: multiples of pi/2 and huge arguments
const PI2: number[] = [Math.PI / 2, Math.PI, 3 * Math.PI / 2, 2 * Math.PI, 1e6 * Math.PI, 2 ** 20 * Math.PI, 1e22, 2 ** 1023];
row("sin*", PI2, (x: number) => Math.sin(x));
row("cos*", PI2, (x: number) => Math.cos(x));
row("tan*", PI2.filter((x: number) => x !== 1e22), (x: number) => Math.tan(x));

const PAIRS: number[][] = [[0, 0], [-0, 0], [0, -0], [-0, -0], [1, 0], [-1, 0], [1, -0], [-1, -0], [0, 1], [0, -1],
  [-0, 1], [-0, -1], [Infinity, Infinity], [-Infinity, Infinity], [Infinity, -Infinity], [-Infinity, -Infinity],
  [1, Infinity], [1, -Infinity], [-1, Infinity], [-1, -Infinity], [Infinity, 1], [-Infinity, 1], [NaN, 1], [1, NaN],
  [NaN, 0], [0, NaN], [3, 4], [-3, 4], [3, -4], [-3, -4], [5e-324, 5e-324], [1e308, 1e-308]];
console.log("atan2", PAIRS.map((p: number[]) => show(Math.atan2(p[0], p[1]))).join(" "));

// pow and ** (math_pow_fdlibm.ts has the cases where fdlibm and the C library differ): the ECMAScript special cases ((+-1) ** (+-Infinity) is NaN, NaN ** 0 is 1, ...), negative
// bases with integer and non-integer exponents, exact powers, overflow and underflow
const POW: number[][] = [...PAIRS, [2, 0.5], [-2, 0.5], [-0, 0.5], [-Infinity, 0.5], [4, 0.5], [4, -0.5],
  [-8, 1 / 3], [-8, 3], [-8, -3], [-2, 1023], [-2, 1024], [-2, -1074], [-2, -1075], [2, -1074], [2, 1024],
  [10, 22], [10, 308], [10, 309], [-10, 21], [0.5, 1074], [0.5, 1075], [-0.5, 1075],
  [-0, 3], [-0, -3], [-0, 2], [-0, -2], [-0, 2.5], [-Infinity, 3], [-Infinity, -3], [-Infinity, 2], [-Infinity, 2.5],
  [1.0000000000000002, 2 ** 60], [0.9999999999999999, 2 ** 60], [-1.0000000000000002, 2 ** 60], [7, 2],
  [9007199254740993, 1], [3, 33], [-3, 33], [1.5, 2], [1e-200, 2], [1e200, 2], [-1e200, 3]];
console.log("pow", POW.map((p: number[]) => show(Math.pow(p[0], p[1]))).join(" "));
console.log("**", POW.map((p: number[]) => show(p[0] ** p[1])).join(" "));
let acc = 1.5;
for (let i = 0; i < 20; i++) acc = acc ** 1.25 - i;
console.log("**loop", show(acc));

// hypot: V8's algorithm (scaled by the largest argument, compensated sum) for any number of arguments
console.log("hypot", show(Math.hypot()), show(Math.hypot(-3)), show(Math.hypot(3, 4)), show(Math.hypot(1, 1)),
  show(Math.hypot(1e308, 1e308)), show(Math.hypot(5e-324, 5e-324)), show(Math.hypot(1, 2, 3)),
  show(Math.hypot(0.1, 0.2, 0.3, 0.4, 0.5)), show(Math.hypot(NaN, Infinity)), show(Math.hypot(NaN, 1)),
  show(Math.hypot(-0, -0)), show(Math.hypot(1e-300, 1e300, 1)), show(Math.hypot(0.7, 0.9, 1.3)));
