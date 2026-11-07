const B = (u8) => Buffer.from(u8.buffer, u8.byteOffset, u8.byteLength);
const U = (buf) => new Uint8Array(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
export const hexEncode = (b) => B(b).toString("hex");
export const hexDecode = (s) => { if (s.length % 2 || /[^0-9a-fA-F]/.test(s)) throw new Error("hexDecode"); return U(Buffer.from(s, "hex")); };
export const base64Encode = (b) => B(b).toString("base64");
export const base64Decode = (s) => U(Buffer.from(s, "base64"));
export const base64UrlEncode = (b) => B(b).toString("base64url");
export const base64UrlDecode = (s) => U(Buffer.from(s, "base64url"));
const A32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
export const base32Encode = (b) => {
  let bits = 0, acc = 0, out = "";
  for (const x of b) { acc = (acc << 8) | x; bits += 8; while (bits >= 5) { bits -= 5; out += A32[(acc >>> bits) & 31]; } acc &= (1 << bits) - 1; }
  if (bits) out += A32[(acc << (5 - bits)) & 31];
  while (out.length % 8) out += "=";
  return out;
};
export const base32Decode = (s) => {
  let bits = 0, acc = 0; const out = [];
  for (const c of s.replace(/=+$/, "")) { acc = (acc << 5) | A32.indexOf(c.toUpperCase()); bits += 5; if (bits >= 8) { bits -= 8; out.push((acc >>> bits) & 255); } acc &= (1 << bits) - 1; }
  return new Uint8Array(out);
};
export const utf8Encode = (s) => new TextEncoder().encode(s);
export const utf8Decode = (b) => new TextDecoder().decode(b);
export const pemDecode = (text, label) => {
  const re = /-----BEGIN ([^-]+)-----([\s\S]*?)-----END \1-----/g;
  let m;
  while ((m = re.exec(text))) if (!label || m[1] === label) return base64Decode(m[2].replace(/\s+/g, ""));
  throw new Error("pemDecode: no PEM block");
};
export const pemEncode = (b, label) => {
  const body = base64Encode(b).match(/.{1,64}/g) ?? [];
  return `-----BEGIN ${label}-----\n${body.join("\n")}\n-----END ${label}-----\n`;
};
export const concatBytes = (...parts) => U(Buffer.concat(parts.map(B)));
export const bytesEqual = (a, b) => B(a).equals(B(b));
export const crc32 = (d) => { let x = ~0 >>> 0; for (const b of d) { x ^= b; for (let k = 0; k < 8; k++) x = x & 1 ? (0xedb88320 ^ (x >>> 1)) >>> 0 : x >>> 1; } return (~x) >>> 0; };
