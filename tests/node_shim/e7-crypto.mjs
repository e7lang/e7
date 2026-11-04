import * as c from "node:crypto";
const B = (u8) => Buffer.from(u8.buffer, u8.byteOffset, u8.byteLength);
const U = (buf) => new Uint8Array(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
const H = { md5: "md5", sha1: "sha1", sha224: "sha224", sha256: "sha256", sha384: "sha384", sha512: "sha512",
  "sha3-224": "sha3-224", "sha3-256": "sha3-256", "sha3-384": "sha3-384", "sha3-512": "sha3-512",
  "sha3_224": "sha3-224", "sha3_256": "sha3-256", "sha3_384": "sha3-384", "sha3_512": "sha3-512" };
const norm = (alg) => { const a = alg.toLowerCase().replace(/^sha-(\d)/, "sha$1"); if (!H[a]) throw new Error("unknown hash " + alg); return H[a]; };
const dg = (alg) => (d) => U(c.createHash(alg).update(B(d)).digest());
export const md5 = dg("md5"), sha1 = dg("sha1"), sha224 = dg("sha224"), sha256 = dg("sha256"), sha384 = dg("sha384"), sha512 = dg("sha512");
export const sha3_224 = dg("sha3-224"), sha3_256 = dg("sha3-256"), sha3_384 = dg("sha3-384"), sha3_512 = dg("sha3-512");
export const hash = (alg, d) => dg(norm(alg))(d);
export const hmac = (alg, key, d) => U(c.createHmac(norm(alg), B(key)).update(B(d)).digest());
export const hkdf = (alg, ikm, salt, info, len) => new Uint8Array(c.hkdfSync(norm(alg), B(ikm), B(salt), B(info), len));
export const pbkdf2 = (alg, pw, salt, it, len) => U(c.pbkdf2Sync(B(pw), B(salt), it, len, norm(alg)));
export const crc32 = (d) => { let x = ~0 >>> 0; for (const b of d) { x ^= b; for (let k = 0; k < 8; k++) x = x & 1 ? (0xedb88320 ^ (x >>> 1)) >>> 0 : x >>> 1; } return (~x) >>> 0; };
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
export const aesCbcEncrypt = (k, iv, d) => { const ci = c.createCipheriv(aesName(k, "cbc"), B(k), B(iv)); return U(Buffer.concat([ci.update(B(d)), ci.final()])); };
export const aesCbcDecrypt = (k, iv, d) => { try { const de = c.createDecipheriv(aesName(k, "cbc"), B(k), B(iv)); return U(Buffer.concat([de.update(B(d)), de.final()])); } catch { return null; } };
export const aesCtr = (k, iv, d) => { const ci = c.createCipheriv(aesName(k, "ctr"), B(k), B(iv)); return U(Buffer.concat([ci.update(B(d)), ci.final()])); };
const edPriv = (seed) => c.createPrivateKey({ key: Buffer.concat([Buffer.from("302e020100300506032b657004220420", "hex"), B(seed)]), format: "der", type: "pkcs8" });
const edPub = (pk) => c.createPublicKey({ key: Buffer.concat([Buffer.from("302a300506032b6570032100", "hex"), B(pk)]), format: "der", type: "spki" });
export const ed25519PublicKey = (seed) => U(c.createPublicKey(edPriv(seed)).export({ format: "der", type: "spki" }).subarray(12));
export const ed25519Sign = (seed, msg) => U(c.sign(null, B(msg), edPriv(seed)));
export const ed25519Verify = (pk, msg, sig) => { try { return c.verify(null, B(msg), edPub(pk), B(sig)); } catch { return false; } };
const curves = { "P-256": "prime256v1", "P-384": "secp384r1", "P-521": "secp521r1" };
const ecPriv = (curve, d) => { const e = c.createECDH(curves[curve]); e.setPrivateKey(B(d)); const pub = e.getPublicKey();
  const n = (curve === "P-521" ? 66 : curve === "P-384" ? 48 : 32);
  return c.createPrivateKey({ key: { kty: "EC", crv: curve, d: B(d).toString("base64url"), x: pub.subarray(1, 1 + n).toString("base64url"), y: pub.subarray(1 + n).toString("base64url") }, format: "jwk" }); };
export const ecdsaPublicKey = (curve, d) => { const e = c.createECDH(curves[curve]); e.setPrivateKey(B(d)); return U(e.getPublicKey()); };
export const ecdsaSign = (curve, alg, d, msg) => U(c.sign(norm(alg), B(msg), { key: ecPriv(curve, d), dsaEncoding: "ieee-p1363" }));
export const ecdsaVerify = (curve, alg, pub, msg, sig) => {
  try {
    const n = (curve === "P-521" ? 66 : curve === "P-384" ? 48 : 32);
    const p = B(pub);
    const key = p[0] === 0x30 ? c.createPublicKey({ key: p, format: "der", type: "spki" })
      : c.createPublicKey({ key: { kty: "EC", crv: curve, x: p.subarray(1, 1 + n).toString("base64url"), y: p.subarray(1 + n).toString("base64url") }, format: "jwk" });
    const raw = sig.length === 2 * n;
    return c.verify(norm(alg), B(msg), { key, dsaEncoding: raw ? "ieee-p1363" : "der" }, B(sig));
  } catch { return false; }
};
const rsaKey = (der, pub) => {
  for (const type of pub ? ["spki", "pkcs1"] : ["pkcs8", "pkcs1"]) {
    try { return pub ? c.createPublicKey({ key: B(der), format: "der", type }) : c.createPrivateKey({ key: B(der), format: "der", type }); } catch {}
  }
  throw new Error("bad RSA key");
};
export const rsaVerify = (alg, pub, msg, sig, padding) => {
  try {
    const opt = { key: rsaKey(pub, true) };
    if (padding === "pss") { opt.padding = c.constants.RSA_PKCS1_PSS_PADDING; opt.saltLength = c.constants.RSA_PSS_SALTLEN_AUTO; }
    return c.verify(norm(alg), B(msg), opt, B(sig));
  } catch { return false; }
};
export const rsaSign = (alg, priv, msg, padding) => {
  const opt = { key: rsaKey(priv, false) };
  if (padding === "pss") { opt.padding = c.constants.RSA_PKCS1_PSS_PADDING; opt.saltLength = c.constants.RSA_PSS_SALTLEN_DIGEST; }
  return U(c.sign(norm(alg), B(msg), opt));
};
export const x25519 = (priv, pub) => {
  const k = c.createPrivateKey({ key: Buffer.concat([Buffer.from("302e020100300506032b656e04220420", "hex"), B(priv)]), format: "der", type: "pkcs8" });
  const p = c.createPublicKey({ key: Buffer.concat([Buffer.from("302a300506032b656e032100", "hex"), B(pub)]), format: "der", type: "spki" });
  return U(c.diffieHellman({ privateKey: k, publicKey: p }));
};
