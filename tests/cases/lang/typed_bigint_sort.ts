// BigUint64Array / BigInt64Array sort without a comparator: unsigned vs signed 64-bit order
const u = new BigUint64Array([18446744073709551615n, 0n, 5n, 9223372036854775808n, 3n, 5n]);
u.sort();
console.log(u.join(","));
const s = new BigInt64Array([-1n, 5n, -9223372036854775808n, 0n, 9223372036854775807n, -1n]);
s.sort();
console.log(s.join(","));
console.log(new BigUint64Array(0).sort().length, BigUint64Array.of(7n).sort().join(","));
