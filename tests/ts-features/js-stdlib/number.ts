// @feature: `Number` methods, constants and formatting
// @category: JavaScript: Standard Library
// @status: supported
console.log((1.005).toFixed(2), (255).toString(16), (255).toString(2), (0.1 + 0.2).toPrecision(3), (123.456).toExponential(2));
console.log(Number.isInteger(5), Number.isSafeInteger(2 ** 53), Number.isNaN(NaN), Number.isFinite(1 / 0));
console.log(Number.MAX_SAFE_INTEGER, Number.EPSILON, Number.MIN_VALUE, Number.MAX_VALUE, Number("0x10"), Number(""), Number("12px"));
console.log(parseInt("42px"), parseInt("ff", 16), parseFloat("3.14abc"), isNaN(Number("x")), isFinite(1e308 * 10), 1e21, 1 / 3);
