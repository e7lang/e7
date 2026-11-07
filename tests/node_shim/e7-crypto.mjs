import * as c from "node:crypto";
const B = (u8) => Buffer.from(u8.buffer, u8.byteOffset, u8.byteLength);
const U = (buf) => new Uint8Array(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
const H = { sha256: "sha256", sha384: "sha384", sha512: "sha512" };
const norm = (alg) => { const a = alg.toLowerCase().replace(/^sha[-_](\d)/, "sha$1"); if (!H[a]) throw new Error("unknown hash " + alg); return H[a]; };
const dg = (alg) => (d) => U(c.createHash(alg).update(B(d)).digest());
export const sha256 = dg("sha256"), sha384 = dg("sha384"), sha512 = dg("sha512");
export const hash = (alg, d) => dg(norm(alg))(d);
export const hmac = (alg, key, d) => U(c.createHmac(norm(alg), B(key)).update(B(d)).digest());
export const hkdf = (alg, ikm, salt, info, len) => new Uint8Array(c.hkdfSync(norm(alg), B(ikm), B(salt), B(info), len));
export const pbkdf2 = (alg, pw, salt, it, len) => U(c.pbkdf2Sync(B(pw), B(salt), it, len, norm(alg)));
export const timingSafeEqual = (a, b) => a.length === b.length && c.timingSafeEqual(B(a), B(b));
export const randomBytes = (n) => U(c.randomBytes(n));
const aead = (name, key, iv, data, aad, enc) => {
  if (enc) {
    const ci = c.createCipheriv(name, B(key), B(iv), { authTagLength: 16 });
    if (aad) ci.setAAD(B(aad));
    return U(Buffer.concat([ci.update(B(data)), ci.final(), ci.getAuthTag()]));
  }
  try {
    const d = B(data);
    const de = c.createDecipheriv(name, B(key), B(iv), { authTagLength: 16 });
    if (aad) de.setAAD(B(aad));
    de.setAuthTag(d.subarray(d.length - 16));
    return U(Buffer.concat([de.update(d.subarray(0, d.length - 16)), de.final()]));
  } catch { return null; }
};
const aesName = (key, mode) => `aes-${key.length * 8}-${mode}`;
export const aesGcmEncrypt = (k, iv, d, aad) => aead(aesName(k, "gcm"), k, iv, d, aad, true);
export const aesGcmDecrypt = (k, iv, d, aad) => aead(aesName(k, "gcm"), k, iv, d, aad, false);
export const chacha20Poly1305Encrypt = (k, iv, d, aad) => aead("chacha20-poly1305", k, iv, d, aad, true);
export const chacha20Poly1305Decrypt = (k, iv, d, aad) => aead("chacha20-poly1305", k, iv, d, aad, false);
const edPriv = (seed) => c.createPrivateKey({ key: Buffer.concat([Buffer.from("302e020100300506032b657004220420", "hex"), B(seed)]), format: "der", type: "pkcs8" });
const edPub = (pk) => c.createPublicKey({ key: Buffer.concat([Buffer.from("302a300506032b6570032100", "hex"), B(pk)]), format: "der", type: "spki" });
export const ed25519PublicKey = (seed) => U(c.createPublicKey(edPriv(seed)).export({ format: "der", type: "spki" }).subarray(12));
export const ed25519Sign = (seed, msg) => U(c.sign(null, B(msg), edPriv(seed)));
export const ed25519Verify = (pk, msg, sig) => { try { return c.verify(null, B(msg), edPub(pk), B(sig)); } catch { return false; } };
export const x25519 = (priv, pub) => {
  const k = c.createPrivateKey({ key: Buffer.concat([Buffer.from("302e020100300506032b656e04220420", "hex"), B(priv)]), format: "der", type: "pkcs8" });
  const p = c.createPublicKey({ key: Buffer.concat([Buffer.from("302a300506032b656e032100", "hex"), B(pub)]), format: "der", type: "spki" });
  return U(c.diffieHellman({ privateKey: k, publicKey: p }));
};
