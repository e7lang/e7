const a = 123456789012345678901234567890n;
function mkB(): bigint { return 987654321098765432109876543210n; }
const b = mkB();
console.log(a + b, a - b, a * b, b / a, b % a, -a, a ** 3n);
console.log(a < b, a > b, a === 123456789012345678901234567890n, a !== b, a <= a);
console.log((-7n) / 2n, (-7n) % 2n, 7n / -2n, (-7n) >> 1n, 7n << 100n, (1n << 64n) - 1n);
console.log(0xffn & 0x0fn, 0xf0n | 0x0fn, 0xffn ^ 0x0fn, ~5n, -5n & 0xffn, -1n | 0n);
console.log(BigInt(2 ** 60), BigInt("12345678901234567890"), BigInt("0x1fffffffffffff"), BigInt.asUintN(64, -1n), BigInt.asIntN(8, 255n));
console.log(a.toString(16), b.toString(36), Number(1n << 70n), String(-42n), `${10n ** 20n}`);
function modpow(base: bigint, exp: bigint, mod: bigint): bigint {
  let result = 1n;
  base %= mod;
  while (exp > 0n) {
    if (exp & 1n) result = (result * base) % mod;
    base = (base * base) % mod;
    exp >>= 1n;
  }
  return result;
}
const p = (1n << 127n) - 1n;
console.log(modpow(3n, p - 1n, p), modpow(2n, 1000n, 1000000007n));
let f = 1n;
for (let i = 1n; i <= 30n; i++) f *= i;
console.log(f, f.toString().length);
const m = new Map<bigint, string>();
m.set(10n ** 20n, "big");
console.log(m.get(100000000000000000000n), m.has(5n));
console.log(JSON.stringify({ n: String(5n) }), typeof a, a === a, 0n === -0n);
