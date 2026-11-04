// SHA-256 written in E7 using u32 arithmetic (portable to Node via the shim)
const K: u32[] = [0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
  0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
  0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb,
  0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f,
  0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2];
function sha256(msg: Uint8Array): string {
  const H: u32[] = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
  const len = msg.length;
  const total = Math.ceil((len + 9) / 64) * 64;
  const buf = new Uint8Array(total);
  buf.set(msg, 0);
  buf[len] = 0x80;
  const bits = len * 8;
  buf[total - 4] = (bits >>> 24) & 255; buf[total - 3] = (bits >>> 16) & 255; buf[total - 2] = (bits >>> 8) & 255; buf[total - 1] = bits & 255;
  const w: u32[] = new Array<u32>(64).fill(0);
  for (let off = 0; off < total; off += 64) {
    for (let t = 0; t < 16; t++) {
      const p = off + t * 4;
      w[t] = u32(u32(buf[p]) << 24 | u32(buf[p + 1]) << 16 | u32(buf[p + 2]) << 8 | u32(buf[p + 3]));
    }
    for (let t = 16; t < 64; t++) {
      const x = w[t - 15], y = w[t - 2];
      const s0 = rotr32(x, 7) ^ rotr32(x, 18) ^ (x >>> 3);
      const s1 = rotr32(y, 17) ^ rotr32(y, 19) ^ (y >>> 10);
      w[t] = u32(w[t - 16] + s0 + w[t - 7] + s1);
    }
    let a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
    for (let t = 0; t < 64; t++) {
      const S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
      const ch = (e & f) ^ (~e & g);
      const t1 = u32(h + S1 + ch + K[t] + w[t]);
      const S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
      const maj = (a & b) ^ (a & c) ^ (b & c);
      const t2 = u32(S0 + maj);
      h = g; g = f; f = e; e = u32(d + t1); d = c; c = b; b = a; a = u32(t1 + t2);
    }
    H[0] = u32(H[0] + a); H[1] = u32(H[1] + b); H[2] = u32(H[2] + c); H[3] = u32(H[3] + d);
    H[4] = u32(H[4] + e); H[5] = u32(H[5] + f); H[6] = u32(H[6] + g); H[7] = u32(H[7] + h);
  }
  return H.map(x => x.toString(16).padStart(8, "0")).join("");
}
const data = new Uint8Array(1 << 20);
for (let i = 0; i < data.length; i++) data[i] = i & 255;
const t0 = Date.now();
const d = sha256(data);
const ms = Date.now() - t0;
console.log(d, "sha256 1MiB", ms, "ms");
console.log(sha256(new TextEncoder().encode("abc")));
