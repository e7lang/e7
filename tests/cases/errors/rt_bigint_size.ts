// @runtime-error: BigInt too large (an e7 limit
const x = 1n << 300000n;
console.log(x.toString().length);
