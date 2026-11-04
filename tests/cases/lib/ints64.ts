// 64-bit integer semantics (E7-specific, expected output in ints64.out)
const a: u64 = 0xffffffffffffffff;
const b: u64 = 1;
console.log(a, a + b, a * a, a / u64(3), a % u64(7), a >> 60, a >>> 1);
const c: i64 = -9223372036854775808;
console.log(c, c - i64(1), c * i64(-1), c / i64(-1), -c, c >> 63, c >>> 63);
const d: i64 = 123456789012345;
console.log(d * d, d % i64(1000), d / i64(-7), d.toString(16), d.toString(2).length);
console.log(mulhi64(a, a), imulhi64(i64(-2), i64(3)), rotl64(u64(1), 63), clz64(u64(1)), ctz64(u64(0)), bswap64(u64(0x0102030405060708)));
function fnv64(s: string): u64 {
  let h: u64 = 0xcbf29ce484222325;
  for (let i = 0; i < s.length; i++) {
    h ^= u64(s.charCodeAt(i));
    h *= 0x100000001b3;
  }
  return h;
}
console.log(fnv64("hello"), fnv64("").toString(16));
const big = BigInt(a);
console.log(big, big + 1n, u64(big + 1n), i64(BigInt(-1)), u64(-1));
console.log(Number(d), u32(d), i32(d), u8(d), i64(1e19), u64(-1.5));
let sum: i64 = 0;
for (let i: i64 = 0; i < 1000000; i++) sum += i * i;
console.log(sum);
