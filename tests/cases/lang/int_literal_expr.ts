// expressions of number literals for integer types are computed as in JavaScript
const x: u8 = 1e2;
const y: u8 = 1 + 2 + 3;
const z: i32 = -(1 << 4) + ~0;
const w: u32 = 0xFFFFFFFF;
const v: u16 = 300 * 200;
const q: i8 = 10 - 20;
console.log(x, y, z, w, v, q);
