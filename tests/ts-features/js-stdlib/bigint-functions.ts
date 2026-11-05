// @feature: `BigInt` functions
// @category: JavaScript: Standard Library
// @status: supported
const big = 12345678901234567890n;
console.log(BigInt("0x1f"), BigInt(255).toString(2), big * big, BigInt.asIntN(64, 2n ** 63n), BigInt.asUintN(64, -1n), big.toString(36));
const m = new Map<bigint, string>([[1n, "one"]]);
console.log(m.get(1n), 10n > 9n, 2n ** 100n);
