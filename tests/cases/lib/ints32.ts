// Portable fixed-width integer code: every result is wrapped explicitly, so Node (with the shim)
// and E7 (native wrapping) agree.
function fnv1a(s: string): u32 {
  let h: u32 = 0x811c9dc5;
  for (let i = 0; i < s.length; i++) {
    h = u32(h ^ u32(s.charCodeAt(i)));
    h = u32(Math.imul(Number(h), 16777619));
  }
  return h;
}
console.log(fnv1a("hello"), fnv1a(""), fnv1a("The quick brown fox"));
const a: u32 = 0xffffffff;
const b: u32 = 1;
console.log(u32(a + b), u32(a - u32(2)), a >>> 4, u32(a << 4), rotl32(a, 3), rotr32(u32(0x12345678), 8));
const x: i32 = -5;
console.log(i32(x * 3), i32(x >> 1), x % 3, i32(x & 0xff), i32(~x));
console.log(clz32(u32(1)), ctz32(u32(8)), popcnt(u32(0xff00ff)), bswap32(u32(0x11223344)));
const m: u8 = 250;
console.log(u8(m + 10), u8(m * 2), i8(u32(200)), u16(u32(70000)), i16(u32(40000)));
// xorshift32 PRNG
let st: u32 = 2463534242;
function next(): u32 { st = u32(st ^ u32(st << 13)); st = u32(st ^ (st >>> 17)); st = u32(st ^ u32(st << 5)); return st; }
const vals: number[] = [];
for (let i = 0; i < 5; i++) vals.push(Number(next()));
console.log(vals.join(","));
// mixing explicit conversions
const n = 3.99;
console.log(u32(n), i32(-n), u32(-1), u8(-1), i32(4294967295), u32(2 ** 32 + 5));
const big: u32 = 4000000000;
console.log(big, Number(big) / 2, big.toString(16), big > u32(5), big < u32(5));
