// e7:crypto natives: SHA-2 (256/384/512), HMAC, HKDF, PBKDF2, AES-GCM and ChaCha20-Poly1305 on top of
// BearSSL, Ed25519 / X25519 on top of Monocypher. Only modern algorithms: no MD5, SHA-1, SHA-3, CBC/CTR,
// RSA or ECDSA (docs/DESIGN.md section 4).
#include <cstdlib>
#include <cstring>
#include <memory>

#include "../stdlib/util.h"
#include "sign.h"

extern "C" {
#include "bearssl.h"
#include "monocypher.h"
}

namespace e7 {

void registerEncodingNatives(VM& vm);

// ------------------------------------------------------------------ helpers
static BytesObj* outBytes(VM& vm, const uint8_t* p, size_t n) { return vm.newBytesFrom(p, n); }
static std::vector<uint8_t> vec(BytesObj* b) { return std::vector<uint8_t>(b->ptr(), b->ptr() + b->len); }

struct HashInfo {
  const br_hash_class* br = nullptr;
  size_t outLen = 0;
};

// "sha256" | "sha384" | "sha512" (case-insensitive; "SHA-256" and "sha_256" spellings are accepted)
static bool findHash(const std::string& in, HashInfo& h) {
  std::string n;
  for (char c : in) n += (char)tolower((unsigned char)c);
  if (n.rfind("sha-", 0) == 0 || n.rfind("sha_", 0) == 0) n = "sha" + n.substr(4);
  h = HashInfo();
  if (n == "sha256") { h.br = &br_sha256_vtable; h.outLen = 32; }
  else if (n == "sha384") { h.br = &br_sha384_vtable; h.outLen = 48; }
  else if (n == "sha512") { h.br = &br_sha512_vtable; h.outLen = 64; }
  else return false;
  return true;
}
static HashInfo needHash(VM& vm, Slot s) {
  HashInfo h;
  std::string n = strUtf8(S(vm, s));
  if (!findHash(n, h)) vm.error("unsupported hash algorithm '%s' (use sha256, sha384 or sha512)", n.c_str());
  return h;
}

static void hashOnce(const HashInfo& h, const uint8_t* p, size_t n, uint8_t* out) {
  br_hash_compat_context c;
  h.br->init(&c.vtable);
  h.br->update(&c.vtable, p, n);
  h.br->out(&c.vtable, out);
}

struct Hmac {
  br_hmac_key_context kc;
  Hmac(const HashInfo& h, const uint8_t* key, size_t klen) { br_hmac_key_init(&kc, h.br, key, klen); }
  void mac(const uint8_t* const* parts, const size_t* lens, size_t nparts, uint8_t* out) {
    br_hmac_context c;
    br_hmac_init(&c, &kc, 0);
    for (size_t i = 0; i < nparts; i++) br_hmac_update(&c, parts[i], lens[i]);
    br_hmac_out(&c, out);
  }
  void mac1(const uint8_t* p, size_t n, uint8_t* out) { mac(&p, &n, 1, out); }
};

static void randomFill(VM& vm, uint8_t* p, size_t n) {
  if (!n) return;
  br_hmac_drbg_context d;
  br_hmac_drbg_init(&d, &br_sha256_vtable, nullptr, 0);
  br_prng_seeder seeder = br_prng_seeder_system(nullptr);
  if (!seeder || !seeder(&d.vtable)) vm.error("no system random source available");
  br_hmac_drbg_generate(&d, p, n);
}

// ------------------------------------------------------------------ natives: hashing & MAC
static void hashNative(VM& vm, Slot* a, uint32_t, Slot* r, void* ud) {
  const char* nm = static_cast<const char*>(ud);
  HashInfo h;
  findHash(nm, h);
  BytesObj* d = B(vm, a[0]);
  uint8_t out[64];
  hashOnce(h, d->ptr(), d->len, out);
  r->r = outBytes(vm, out, h.outLen);
}
E7_NATIVE(c_hash) {
  E7_UNUSED;
  HashInfo h = needHash(vm, a[0]);
  BytesObj* d = B(vm, a[1]);
  uint8_t out[64];
  hashOnce(h, d->ptr(), d->len, out);
  r->r = outBytes(vm, out, h.outLen);
}
E7_NATIVE(c_hmac) {
  E7_UNUSED;
  HashInfo h = needHash(vm, a[0]);
  BytesObj* k = B(vm, a[1]);
  BytesObj* d = B(vm, a[2]);
  Hmac m(h, k->ptr(), k->len);
  uint8_t out[64];
  m.mac1(d->ptr(), d->len, out);
  r->r = outBytes(vm, out, h.outLen);
}
E7_NATIVE(c_hmacKey) {
  E7_UNUSED;
  HashInfo h = needHash(vm, a[0]);
  Obj* ho = a[1].r;
  if (!ho || ho->type != OT::Handle) vm.error("invalid key handle");
  int64_t id = static_cast<HandleObj*>(ho)->id;
  const uint8_t* key = nullptr;
  size_t klen = 0;
  if (!vm.hostKeyFn || vm.hostKeyFn(vm.hostKeyUd, id, &key, &klen) != 0 || (!key && klen))
    vm.error("key handle %lld is not available", (long long)id);
  BytesObj* d = B(vm, a[2]);
  static const uint8_t kEmpty[1] = {0};
  Hmac m(h, key ? key : kEmpty, klen);  // a provider may return NULL for an empty key
  uint8_t out[64];
  m.mac1(d->ptr(), d->len, out);
  r->r = outBytes(vm, out, h.outLen);
}
E7_NATIVE(c_hkdf) {
  E7_UNUSED;
  HashInfo h = needHash(vm, a[0]);
  BytesObj *ikm = B(vm, a[1]), *salt = B(vm, a[2]), *info = B(vm, a[3]);
  double L = toInteger(a[4].f);
  if (L < 0 || L > 255.0 * h.outLen) vm.error("hkdf: invalid output length");
  // extract
  std::vector<uint8_t> s0 = vec(salt);
  if (s0.empty()) s0.assign(h.outLen, 0);
  uint8_t prk[64];
  Hmac ex(h, s0.data(), s0.size());
  ex.mac1(ikm->ptr(), ikm->len, prk);
  // expand
  Hmac exp(h, prk, h.outLen);
  std::vector<uint8_t> okm;
  uint8_t t[64];
  size_t tl = 0;
  for (uint8_t c = 1; okm.size() < (size_t)L; c++) {
    const uint8_t* parts[3] = {t, info->ptr(), &c};
    size_t lens[3] = {tl, info->len, 1};
    exp.mac(parts, lens, 3, t);
    tl = h.outLen;
    okm.insert(okm.end(), t, t + tl);
  }
  okm.resize((size_t)L);
  r->r = outBytes(vm, okm.data(), okm.size());
}
E7_NATIVE(c_pbkdf2) {
  E7_UNUSED;
  HashInfo h = needHash(vm, a[0]);
  BytesObj *pw = B(vm, a[1]), *salt = B(vm, a[2]);
  double it = toInteger(a[3].f), L = toInteger(a[4].f);
  if (it < 1 || it > 1e8) vm.error("pbkdf2: invalid iteration count");
  if (L < 0 || L > (1 << 20)) vm.error("pbkdf2: invalid key length");
  vm.chargeWork((uint64_t)it * (((uint64_t)L + h.outLen - 1) / h.outLen));  // one tick per HMAC
  Hmac m(h, pw->ptr(), pw->len);
  std::vector<uint8_t> out;
  uint8_t u[64], t[64];
  std::vector<uint8_t> s = vec(salt);
  for (uint32_t blk = 1; out.size() < (size_t)L; blk++) {
    uint8_t be[4] = {(uint8_t)(blk >> 24), (uint8_t)(blk >> 16), (uint8_t)(blk >> 8), (uint8_t)blk};
    const uint8_t* parts[2] = {s.data(), be};
    size_t lens[2] = {s.size(), 4};
    m.mac(parts, lens, 2, u);
    memcpy(t, u, h.outLen);
    for (uint64_t k = 1; k < (uint64_t)it; k++) {
      m.mac1(u, h.outLen, u);
      for (size_t j = 0; j < h.outLen; j++) t[j] ^= u[j];
    }
    out.insert(out.end(), t, t + h.outLen);
  }
  out.resize((size_t)L);
  r->r = outBytes(vm, out.data(), out.size());
}
E7_NATIVE(c_randomBytes) {
  E7_UNUSED;
  double n0 = toInteger(a[0].f);
  if (n0 < 0 || n0 > (1 << 24)) vm.error("randomBytes: invalid size");
  std::vector<uint8_t> b((size_t)n0);
  randomFill(vm, b.data(), b.size());
  r->r = outBytes(vm, b.data(), b.size());
}
E7_NATIVE(c_timingSafeEqual) {
  E7_UNUSED;
  BytesObj *x = B(vm, a[0]), *y = B(vm, a[1]);
  if (x->len != y->len) { r->i = 0; return; }
  uint8_t acc = 0;
  for (uint32_t i = 0; i < x->len; i++) acc |= x->ptr()[i] ^ y->ptr()[i];
  r->i = acc == 0;
}

// ------------------------------------------------------------------ ciphers
static Slot gcm(VM& vm, Slot* a, bool enc) {
  BytesObj *k = B(vm, a[0]), *iv = B(vm, a[1]), *d = B(vm, a[2]);
  if (k->len != 16 && k->len != 24 && k->len != 32) vm.error("AES key must be 16, 24 or 32 bytes");
  if (iv->len == 0) vm.error("GCM IV must not be empty");
  BytesObj* aad = a[3].r ? B(vm, a[3]) : nullptr;
  br_aes_ct_ctr_keys bc;
  br_aes_ct_ctr_init(&bc, k->ptr(), k->len);
  br_gcm_context gc;
  br_gcm_init(&gc, &bc.vtable, br_ghash_ctmul);
  br_gcm_reset(&gc, iv->ptr(), iv->len);
  if (aad) br_gcm_aad_inject(&gc, aad->ptr(), aad->len);
  br_gcm_flip(&gc);
  Slot r;
  if (enc) {
    std::vector<uint8_t> out(d->ptr(), d->ptr() + d->len);
    br_gcm_run(&gc, 1, out.data(), out.size());
    uint8_t tag[16];
    br_gcm_get_tag(&gc, tag);
    out.insert(out.end(), tag, tag + 16);
    r.r = outBytes(vm, out.data(), out.size());
    return r;
  }
  if (d->len < 16) { r.r = nullptr; return r; }
  std::vector<uint8_t> out(d->ptr(), d->ptr() + d->len - 16);
  br_gcm_run(&gc, 0, out.data(), out.size());
  if (!br_gcm_check_tag(&gc, d->ptr() + d->len - 16)) { r.r = nullptr; return r; }
  r.r = outBytes(vm, out.data(), out.size());
  return r;
}
E7_NATIVE(c_gcmEnc) { E7_UNUSED; *r = gcm(vm, a, true); }
E7_NATIVE(c_gcmDec) { E7_UNUSED; *r = gcm(vm, a, false); }
static Slot chachaPoly(VM& vm, Slot* a, bool enc) {
  BytesObj *k = B(vm, a[0]), *iv = B(vm, a[1]), *d = B(vm, a[2]);
  if (k->len != 32 || iv->len != 12) vm.error("ChaCha20-Poly1305 requires a 32-byte key and 12-byte nonce");
  BytesObj* aad = a[3].r ? B(vm, a[3]) : nullptr;
  Slot r;
  uint8_t tag[16];
  if (enc) {
    std::vector<uint8_t> out(d->ptr(), d->ptr() + d->len);
    br_poly1305_ctmul_run(k->ptr(), iv->ptr(), out.data(), out.size(), aad ? aad->ptr() : nullptr, aad ? aad->len : 0, tag,
                          br_chacha20_ct_run, 1);
    out.insert(out.end(), tag, tag + 16);
    r.r = outBytes(vm, out.data(), out.size());
    return r;
  }
  if (d->len < 16) { r.r = nullptr; return r; }
  std::vector<uint8_t> out(d->ptr(), d->ptr() + d->len - 16);
  br_poly1305_ctmul_run(k->ptr(), iv->ptr(), out.data(), out.size(), aad ? aad->ptr() : nullptr, aad ? aad->len : 0, tag,
                        br_chacha20_ct_run, 0);
  uint8_t acc = 0;
  for (int i = 0; i < 16; i++) acc |= tag[i] ^ d->ptr()[d->len - 16 + i];
  if (acc) { r.r = nullptr; return r; }
  r.r = outBytes(vm, out.data(), out.size());
  return r;
}
E7_NATIVE(c_chachaEnc) { E7_UNUSED; *r = chachaPoly(vm, a, true); }
E7_NATIVE(c_chachaDec) { E7_UNUSED; *r = chachaPoly(vm, a, false); }
// ------------------------------------------------------------------ Ed25519 / X25519
E7_NATIVE(c_edPub) {
  E7_UNUSED;
  BytesObj* s = B(vm, a[0]);
  if (s->len != 32) vm.error("Ed25519 private key (seed) must be 32 bytes");
  uint8_t pk[32];
  ed25519PublicKey(s->ptr(), pk);
  r->r = outBytes(vm, pk, 32);
}
E7_NATIVE(c_edSign) {
  E7_UNUSED;
  BytesObj *s = B(vm, a[0]), *m = B(vm, a[1]);
  if (s->len != 32) vm.error("Ed25519 private key (seed) must be 32 bytes");
  uint8_t sig[64];
  ed25519Sign(s->ptr(), m->ptr(), m->len, sig);
  r->r = outBytes(vm, sig, 64);
}
E7_NATIVE(c_edVerify) {
  E7_UNUSED;
  BytesObj *pk = B(vm, a[0]), *m = B(vm, a[1]), *sig = B(vm, a[2]);
  r->i = pk->len == 32 && sig->len == 64 && ed25519Verify(pk->ptr(), m->ptr(), m->len, sig->ptr());
}
E7_NATIVE(c_x25519) {
  E7_UNUSED;
  BytesObj *sk = B(vm, a[0]), *pk = B(vm, a[1]);
  if (sk->len != 32 || pk->len != 32) vm.error("X25519 keys must be 32 bytes");
  uint8_t out[32];
  crypto_x25519(out, sk->ptr(), pk->ptr());
  uint8_t acc = 0;
  for (int i = 0; i < 32; i++) acc |= out[i];  // constant time
  if (acc == 0) vm.error("X25519: low-order public key (all-zero shared secret)");  // RFC 7748 6.1
  r->r = outBytes(vm, out, 32);
}

void registerCryptoNatives(VM& vm) {
  registerEncodingNatives(vm);
  static const char* hashes[] = {"sha256", "sha384", "sha512"};
  for (const char* h : hashes) vm.registerNative(std::string("crypto.hash.") + h, "R>R", hashNative, (void*)h);
  vm.registerNative("crypto.hash", "RR>R", c_hash);
  vm.registerNative("crypto.hmac", "RRR>R", c_hmac);
  vm.registerNative("crypto.hmacKey", "RRR>R", c_hmacKey);
  vm.registerNative("crypto.hkdf", "RRRRF>R", c_hkdf);
  vm.registerNative("crypto.pbkdf2", "RRRFF>R", c_pbkdf2);
  vm.registerNative("crypto.randomBytes", "F>R", c_randomBytes);
  vm.registerNative("crypto.timingSafeEqual", "RR>I", c_timingSafeEqual);
  vm.registerNative("crypto.aesGcmEncrypt", "RRRR>R", c_gcmEnc);
  vm.registerNative("crypto.aesGcmDecrypt", "RRRR>R", c_gcmDec);
  vm.registerNative("crypto.chacha20Poly1305Encrypt", "RRRR>R", c_chachaEnc);
  vm.registerNative("crypto.chacha20Poly1305Decrypt", "RRRR>R", c_chachaDec);
  vm.registerNative("crypto.ed25519PublicKey", "R>R", c_edPub);
  vm.registerNative("crypto.ed25519Sign", "RR>R", c_edSign);
  vm.registerNative("crypto.ed25519Verify", "RRR>I", c_edVerify);
  vm.registerNative("crypto.x25519", "RR>R", c_x25519);
}

}  // namespace e7
