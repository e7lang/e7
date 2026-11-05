// @feature: `Math`
// @category: JavaScript: Standard Library
// @status: supported
// @note: transcendental functions give the same doubles as Node.js 22 on x64 (fdlibm)
console.log(Math.PI, Math.E, Math.floor(-1.5), Math.ceil(1.2), Math.round(2.5), Math.round(-2.5), Math.trunc(-1.7));
console.log(Math.sqrt(2), Math.cbrt(27), Math.hypot(3, 4), Math.sign(-3), Math.abs(-4), Math.min(), Math.max(1, 5, 3));
console.log(Math.sin(1), Math.log(10), Math.exp(1), Math.atan2(1, 1), Math.log2(8), Math.clz32(1), Math.imul(3, 4), Math.fround(5.1));
const r = Math.random();
console.log(r >= 0 && r < 1);
