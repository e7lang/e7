// A private, server-delivered digest algorithm written directly in E7 with u32 arithmetic.
// (Example of the "custom algorithm" layer; real deployments would rotate constants per release.)
const ROUNDS = 16;
const SALT: u32[] = [0x9e3779b9, 0x7f4a7c15, 0xf39cc060, 0x5ced2a2d];

function mix(a: u32, b: u32, r: number): u32 {
  let x = u32(a ^ rotl32(b, 5 + (r % 11)));
  x = u32(Math.imul(Number(x), 0x85ebca6b));
  x = u32(x ^ (x >>> 13));
  return u32(x + SALT[r % SALT.length]);
}

export function digest(data: Uint8Array): string {
  const s: u32[] = [0x01234567, 0x89abcdef, 0xfedcba98, 0x76543210];
  for (let i = 0; i < data.length; i++) {
    const k = i % 4;
    s[k] = mix(s[k], u32(u32(data[i]) << ((i % 4) * 8)), i);
  }
  for (let r = 0; r < ROUNDS; r++) {
    for (let k = 0; k < 4; k++) s[k] = mix(s[k], s[(k + 1) % 4], r + k);
  }
  return s.map(w => w.toString(16).padStart(8, "0")).join("");
}

console.log(digest(new TextEncoder().encode("")));
console.log(digest(new TextEncoder().encode("hello world")));
console.log(digest(new TextEncoder().encode("hello worle")));
