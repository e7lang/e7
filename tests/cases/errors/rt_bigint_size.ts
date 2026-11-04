// @runtime-error: Maximum BigInt size exceeded
const x = 1n << 300000n;
console.log(x.toString().length);
