// @feature: Arithmetic, comparison, bitwise, exponent and conditional operators
// @category: JavaScript: Syntax
// @status: supported
let x = 7;
x += 3;
x -= 1;
x *= 2;
x /= 3;
x %= 4;
x **= 3;
let i = 0;
const post = i++;
const pre = ++i;
console.log(x, post, pre, 2 ** 10, -7 % 3, 7 / 2, 5 & 3, 5 | 3, 5 ^ 3, ~5, 1 << 31, -1 >>> 0, -8 >> 1);
console.log(1 < 2, "b" > "a", "B" < "a", x > 10 ? "big" : "small", !(x > 1), typeof x);
