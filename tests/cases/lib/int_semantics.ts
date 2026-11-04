// E7-specific integer semantics (differs from JS numbers by design)
const a: u32 = 0xffffffff;
console.log(a * a, a >> 4, u32(3) / u32(2), u32(7) % u32(4));
const x: i32 = -5;
console.log(x / 2, x >>> 28, x >> 1, x * 1000000000);
const y: u8 = 200;
console.log(y + 100, y << 1, y >> 1, -y);
const z: i16 = 32767;
console.log(z + 1, z * 2, i16(-32768) / i16(-1));
console.log(u32(1) << 31, u32(1) << 32, i32(1) << 31, u64(1) << 64);
