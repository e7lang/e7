// @feature: `bigint` primitive and literals
// @category: Everyday Types
// @status: supported
const big: bigint = 2n ** 64n - 1n;
console.log(big, big.toString(16), typeof big, BigInt(42) * 3n, -7n / 2n, -7n % 2n);
console.log(BigInt.asUintN(8, 257n), BigInt.asIntN(8, 255n), big > 0n);
