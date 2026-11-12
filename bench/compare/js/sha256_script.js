const log = typeof print === "function" ? print : console.log;
function rotr(x, n) { return (x >>> n) | (x << (32 - n)); }
// SHA-256 written in E7 using u32 arithmetic (portable to Node via the shim)
const K = [0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
  0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
  0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb,
  0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f,
  0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2];
function sha256(msg) {
  const H = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
  const len = msg.length;
  const total = Math.ceil((len + 9) / 64) * 64;
  const buf = new Uint8Array(total);
  buf.set(msg, 0);
  buf[len] = 0x80;
  const bits = len * 8;
  buf[total - 4] = (bits >>> 24) & 255; buf[total - 3] = (bits >>> 16) & 255; buf[total - 2] = (bits >>> 8) & 255; buf[total - 1] = bits & 255;
  const w = new Array(64).fill(0);
  for (let off = 0; off < total; off += 64) {
    for (let t = 0; t < 16; t++) {
      const p = off + t * 4;
      w[t] = ((((buf[p]) >>> 0) << 24 | ((buf[p + 1]) >>> 0) << 16 | ((buf[p + 2]) >>> 0) << 8 | ((buf[p + 3]) >>> 0)) >>> 0);
    }
    for (let t = 16; t < 64; t++) {
      const x = w[t - 15], y = w[t - 2];
      const s0 = rotr(x, 7) ^ rotr(x, 18) ^ (x >>> 3);
      const s1 = rotr(y, 17) ^ rotr(y, 19) ^ (y >>> 10);
      w[t] = ((w[t - 16] + s0 + w[t - 7] + s1) >>> 0);
    }
    let a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
    for (let t = 0; t < 64; t++) {
      const S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const ch = (e & f) ^ (~e & g);
      const t1 = ((h + S1 + ch + K[t] + w[t]) >>> 0);
      const S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const maj = (a & b) ^ (a & c) ^ (b & c);
      const t2 = ((S0 + maj) >>> 0);
      h = g; g = f; f = e; e = ((d + t1) >>> 0); d = c; c = b; b = a; a = ((t1 + t2) >>> 0);
    }
    H[0] = ((H[0] + a) >>> 0); H[1] = ((H[1] + b) >>> 0); H[2] = ((H[2] + c) >>> 0); H[3] = ((H[3] + d) >>> 0);
    H[4] = ((H[4] + e) >>> 0); H[5] = ((H[5] + f) >>> 0); H[6] = ((H[6] + g) >>> 0); H[7] = ((H[7] + h) >>> 0);
  }
  return H.map(x => x.toString(16).padStart(8, "0")).join("");
}
const data = new Uint8Array(1 << 20);
for (let i = 0; i < data.length; i++) data[i] = i & 255;
const t0 = Date.now();
const d = sha256(data);
const ms = Date.now() - t0;
log(d, "sha256 1MiB", ms, "ms");
