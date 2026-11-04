// e7:crypto natives on top of BearSSL (hashes, HMAC, HKDF, AES, ChaCha20-Poly1305, ECDSA, RSA)
// and Monocypher (Ed25519 / X25519 primitives).
#include <algorithm>
#include <cstring>
#include <memory>

#include "../stdlib/util.h"
#include "sign.h"

extern "C" {
#include "bearssl.h"
#include "monocypher.h"
}

namespace e7 {

uint32_t crc32(const uint8_t* p, size_t n);
void registerEncodingNatives(VM& vm);

// ------------------------------------------------------------------ helpers
static BytesObj* outBytes(VM& vm, const uint8_t* p, size_t n) { return vm.newBytesFrom(p, n); }
static std::vector<uint8_t> vec(BytesObj* b) { return std::vector<uint8_t>(b->ptr(), b->ptr() + b->len); }

struct HashInfo {
  const br_hash_class* br = nullptr;
  size_t sha3Out = 0;
  bool keccak = false;
  size_t outLen = 0;
  size_t blockLen = 0;
  const unsigned char* oid = nullptr;  // for RSA PKCS#1
  std::string name;
};

static const unsigned char* const OID_SHA1 = BR_HASH_OID_SHA1;
static const unsigned char* const OID_SHA224 = BR_HASH_OID_SHA224;
static const unsigned char* const OID_SHA256 = BR_HASH_OID_SHA256;
static const unsigned char* const OID_SHA384 = BR_HASH_OID_SHA384;
static const unsigned char* const OID_SHA512 = BR_HASH_OID_SHA512;

static bool findHash(const std::string& in, HashInfo& h) {
  std::string n;
  for (char c : in) n += (char)tolower((unsigned char)c);
  if (n.rfind("sha-", 0) == 0) n = "sha" + n.substr(4);
  for (auto& c : n) if (c == '_') c = '-';
  h = HashInfo();
  h.name = n;
  if (n == "md5") { h.br = &br_md5_vtable; h.outLen = 16; h.blockLen = 64; }
  else if (n == "sha1") { h.br = &br_sha1_vtable; h.outLen = 20; h.blockLen = 64; h.oid = OID_SHA1; }
  else if (n == "sha224") { h.br = &br_sha224_vtable; h.outLen = 28; h.blockLen = 64; h.oid = OID_SHA224; }
  else if (n == "sha256") { h.br = &br_sha256_vtable; h.outLen = 32; h.blockLen = 64; h.oid = OID_SHA256; }
  else if (n == "sha384") { h.br = &br_sha384_vtable; h.outLen = 48; h.blockLen = 128; h.oid = OID_SHA384; }
  else if (n == "sha512") { h.br = &br_sha512_vtable; h.outLen = 64; h.blockLen = 128; h.oid = OID_SHA512; }
  else if (n == "sha3-224" || n == "sha3-256" || n == "sha3-384" || n == "sha3-512") {
    h.sha3Out = (size_t)atoi(n.c_str() + 5) / 8;
    h.outLen = h.sha3Out;
    h.blockLen = 200 - 2 * h.outLen;
  } else if (n == "keccak256" || n == "keccak-256") {
    h.sha3Out = 32;
    h.keccak = true;
    h.outLen = 32;
    h.blockLen = 136;
  } else return false;
  return true;
}
static HashInfo needHash(VM& vm, Slot s) {
  HashInfo h;
  std::string n = strUtf8(S(vm, s));
  if (!findHash(n, h)) vm.error("unsupported hash algorithm '%s'", n.c_str());
  return h;
}

static void hashOnce(const HashInfo& h, const uint8_t* p, size_t n, uint8_t* out) {
  if (h.br) {
    br_hash_compat_context c;
    h.br->init(&c.vtable);
    h.br->update(&c.vtable, p, n);
    h.br->out(&c.vtable, out);
  } else {
    sha3(p, n, out, h.sha3Out, h.keccak);
  }
}

// Incremental hashing over several parts (generic, works for SHA-3 too)
static void hashParts(const HashInfo& h, std::initializer_list<std::pair<const uint8_t*, size_t>> parts, uint8_t* out) {
  if (h.br) {
    br_hash_compat_context c;
    h.br->init(&c.vtable);
    for (auto& p : parts) h.br->update(&c.vtable, p.first, p.second);
    h.br->out(&c.vtable, out);
    return;
  }
  std::vector<uint8_t> buf;
  for (auto& p : parts) buf.insert(buf.end(), p.first, p.first + p.second);
  sha3(buf.data(), buf.size(), out, h.sha3Out, h.keccak);
}

struct Hmac {
  HashInfo h;
  std::vector<uint8_t> k0;  // key padded to block length
  br_hmac_key_context kc;
  Hmac(const HashInfo& hi, const uint8_t* key, size_t klen) : h(hi) {
    if (h.br) {
      br_hmac_key_init(&kc, h.br, key, klen);
    } else {
      k0.assign(h.blockLen, 0);
      if (klen > h.blockLen) hashOnce(h, key, klen, k0.data());
      else if (klen) memcpy(k0.data(), key, klen);
    }
  }
  void mac(const uint8_t* const* parts, const size_t* lens, size_t nparts, uint8_t* out) {
    if (h.br) {
      br_hmac_context c;
      br_hmac_init(&c, &kc, 0);
      for (size_t i = 0; i < nparts; i++) br_hmac_update(&c, parts[i], lens[i]);
      br_hmac_out(&c, out);
      return;
    }
    std::vector<uint8_t> ip(h.blockLen), op(h.blockLen);
    for (size_t i = 0; i < h.blockLen; i++) { ip[i] = k0[i] ^ 0x36; op[i] = k0[i] ^ 0x5c; }
    std::vector<uint8_t> inner(ip);
    for (size_t i = 0; i < nparts; i++) inner.insert(inner.end(), parts[i], parts[i] + lens[i]);
    uint8_t ih[64];
    hashOnce(h, inner.data(), inner.size(), ih);
    std::vector<uint8_t> outer(op);
    outer.insert(outer.end(), ih, ih + h.outLen);
    hashOnce(h, outer.data(), outer.size(), out);
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

// ------------------------------------------------------------------ minimal DER reader
struct Der {
  const uint8_t* p;
  size_t n;
  size_t i = 0;
  bool ok = true;
  // read TLV; returns pointer to contents
  const uint8_t* read(uint8_t tag, size_t& len) {
    if (!ok || i + 2 > n || p[i] != tag) { ok = false; return nullptr; }
    i++;
    size_t l = p[i++];
    if (l & 0x80) {
      int k = l & 0x7F;
      if (k == 0 || k > 4 || i + k > n) { ok = false; return nullptr; }
      l = 0;
      for (int j = 0; j < k; j++) l = (l << 8) | p[i++];
    }
    if (l > n - i) { ok = false; return nullptr; }
    const uint8_t* c = p + i;
    i += l;
    len = l;
    return c;
  }
  bool peek(uint8_t tag) const { return ok && i < n && p[i] == tag; }
};
static std::vector<uint8_t> intBytes(const uint8_t* p, size_t n) {
  while (n > 1 && *p == 0) { p++; n--; }
  return std::vector<uint8_t>(p, p + n);
}

struct RsaPub {
  std::vector<uint8_t> n, e;
};
static bool parseRsaPub(const uint8_t* p, size_t len, RsaPub& out) {
  Der d{p, len};
  size_t l;
  const uint8_t* seq = d.read(0x30, l);
  if (!seq) return false;
  Der s{seq, l};
  if (s.peek(0x30)) {
    // SubjectPublicKeyInfo
    size_t al;
    s.read(0x30, al);
    size_t bl;
    const uint8_t* bits = s.read(0x03, bl);
    if (!bits || bl < 2 || bits[0] != 0) return false;
    return parseRsaPub(bits + 1, bl - 1, out);
  }
  size_t nl, el;
  const uint8_t* n = s.read(0x02, nl);
  const uint8_t* e = s.read(0x02, el);
  if (!n || !e) return false;
  out.n = intBytes(n, nl);
  out.e = intBytes(e, el);
  return true;
}
struct RsaPriv {
  std::vector<uint8_t> n, e, d, p, q, dp, dq, iq;
  uint32_t bits = 0;
};
static uint32_t bitLen(const std::vector<uint8_t>& v) {
  if (v.empty()) return 0;
  uint32_t b = (uint32_t)(v.size() - 1) * 8;
  uint8_t top = v[0];
  while (top) { b++; top >>= 1; }
  return b;
}
// big-endian magnitude product (operands are at most a few hundred bytes)
static std::vector<uint8_t> mulBE(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  std::vector<uint32_t> acc(a.size() + b.size(), 0);
  for (size_t i = 0; i < a.size(); i++) {
    uint32_t carry = 0;
    size_t ai = a.size() - 1 - i;
    for (size_t j = 0; j < b.size(); j++) {
      size_t k = i + j;
      uint32_t t = acc[k] + (uint32_t)a[ai] * b[b.size() - 1 - j] + carry;
      acc[k] = t & 0xFF;
      carry = t >> 8;
    }
    for (size_t k = i + b.size(); carry; k++) {
      uint32_t t = acc[k] + carry;
      acc[k] = t & 0xFF;
      carry = t >> 8;
    }
  }
  std::vector<uint8_t> out;
  size_t top = acc.size();
  while (top > 0 && acc[top - 1] == 0) top--;
  for (size_t k = top; k-- > 0;) out.push_back((uint8_t)acc[k]);
  return out;
}
static std::vector<uint8_t> stripZeros(const std::vector<uint8_t>& v) {
  size_t i = 0;
  while (i < v.size() && v[i] == 0) i++;
  return std::vector<uint8_t>(v.begin() + (long)i, v.end());
}
static bool parseRsaPrivInner(const uint8_t* p, size_t len, RsaPriv& out, int depth);
// Parses and validates an RSA private key. The CRT engine trusts that n == p*q (it sizes buffers from
// n's length), so a malformed key with an unrelated n must be rejected here.
static bool parseRsaPriv(const uint8_t* p, size_t len, RsaPriv& out) {
  if (!parseRsaPrivInner(p, len, out, 0)) return false;
  if (out.bits < 512 || out.bits > 4096) return false;
  std::vector<uint8_t> pp = stripZeros(out.p), qq = stripZeros(out.q);
  if (pp.empty() || qq.empty() || pp.size() + qq.size() > 520) return false;
  return mulBE(pp, qq) == stripZeros(out.n);
}
static bool parseRsaPrivInner(const uint8_t* p, size_t len, RsaPriv& out, int depth) {
  if (depth > 2) return false;
  Der d{p, len};
  size_t l;
  const uint8_t* seq = d.read(0x30, l);
  if (!seq) return false;
  Der s{seq, l};
  size_t vl;
  const uint8_t* ver = s.read(0x02, vl);
  if (!ver) return false;
  if (s.peek(0x30)) {
    // PKCS#8 PrivateKeyInfo
    size_t al;
    s.read(0x30, al);
    size_t ol;
    const uint8_t* oct = s.read(0x04, ol);
    if (!oct) return false;
    return parseRsaPrivInner(oct, ol, out, depth + 1);
  }
  std::vector<uint8_t>* fields[] = {&out.n, &out.e, &out.d, &out.p, &out.q, &out.dp, &out.dq, &out.iq};
  for (auto* f : fields) {
    size_t fl;
    const uint8_t* fp = s.read(0x02, fl);
    if (!fp) return false;
    *f = intBytes(fp, fl);
  }
  out.bits = bitLen(out.n);
  return true;
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
  Hmac m(h, key, klen);
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
E7_NATIVE(c_crc32) { E7_UNUSED; BytesObj* d = B(vm, a[0]); r->f = crc32(d->ptr(), d->len); }
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
static void checkAesKey(VM& vm, BytesObj* k) {
  if (k->len != 16 && k->len != 24 && k->len != 32) vm.error("AES key must be 16, 24 or 32 bytes");
}
static Slot gcm(VM& vm, Slot* a, bool enc) {
  BytesObj *k = B(vm, a[0]), *iv = B(vm, a[1]), *d = B(vm, a[2]);
  checkAesKey(vm, k);
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
E7_NATIVE(c_cbcEnc) {
  E7_UNUSED;
  BytesObj *k = B(vm, a[0]), *iv = B(vm, a[1]), *d = B(vm, a[2]);
  checkAesKey(vm, k);
  if (iv->len != 16) vm.error("AES-CBC IV must be 16 bytes");
  std::vector<uint8_t> out(d->ptr(), d->ptr() + d->len);
  uint8_t pad = (uint8_t)(16 - out.size() % 16);
  out.insert(out.end(), pad, pad);
  br_aes_ct_cbcenc_keys kc;
  br_aes_ct_cbcenc_init(&kc, k->ptr(), k->len);
  uint8_t ivc[16];
  memcpy(ivc, iv->ptr(), 16);
  br_aes_ct_cbcenc_run(&kc, ivc, out.data(), out.size());
  r->r = outBytes(vm, out.data(), out.size());
}
E7_NATIVE(c_cbcDec) {
  E7_UNUSED;
  BytesObj *k = B(vm, a[0]), *iv = B(vm, a[1]), *d = B(vm, a[2]);
  checkAesKey(vm, k);
  if (iv->len != 16) vm.error("AES-CBC IV must be 16 bytes");
  if (d->len == 0 || d->len % 16) { r->r = nullptr; return; }
  std::vector<uint8_t> out(d->ptr(), d->ptr() + d->len);
  br_aes_ct_cbcdec_keys kc;
  br_aes_ct_cbcdec_init(&kc, k->ptr(), k->len);
  uint8_t ivc[16];
  memcpy(ivc, iv->ptr(), 16);
  br_aes_ct_cbcdec_run(&kc, ivc, out.data(), out.size());
  uint8_t pad = out.back();
  bool bad = pad == 0 || pad > 16;
  if (!bad)
    for (size_t i = 0; i < pad; i++) bad |= out[out.size() - 1 - i] != pad;
  if (bad) { r->r = nullptr; return; }
  out.resize(out.size() - pad);
  r->r = outBytes(vm, out.data(), out.size());
}
E7_NATIVE(c_ctr) {
  E7_UNUSED;
  BytesObj *k = B(vm, a[0]), *iv = B(vm, a[1]), *d = B(vm, a[2]);
  checkAesKey(vm, k);
  if (iv->len != 16) vm.error("AES-CTR counter block must be 16 bytes");
  br_aes_ct_ctr_keys kc;
  br_aes_ct_ctr_init(&kc, k->ptr(), k->len);
  std::vector<uint8_t> out(d->ptr(), d->ptr() + d->len);
  uint8_t nonce[12];
  memcpy(nonce, iv->ptr(), 12);
  uint32_t cc = ((uint32_t)iv->ptr()[12] << 24) | ((uint32_t)iv->ptr()[13] << 16) | ((uint32_t)iv->ptr()[14] << 8) | iv->ptr()[15];
  size_t off = 0;
  while (off < out.size()) {
    uint64_t untilWrap = ((uint64_t)1 << 32) - cc;
    size_t chunk = (size_t)std::min<uint64_t>(out.size() - off, untilWrap * 16);
    uint32_t ncc = br_aes_ct_ctr_run(&kc, nonce, cc, out.data() + off, chunk);
    off += chunk;
    if ((uint64_t)cc + (chunk + 15) / 16 >= ((uint64_t)1 << 32)) {
      // 32-bit counter wrapped: carry into the upper 96 bits (128-bit counter semantics)
      for (int i = 11; i >= 0; i--)
        if (++nonce[i]) break;
    }
    cc = ncc;
  }
  r->r = outBytes(vm, out.data(), out.size());
}

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
  r->r = outBytes(vm, out, 32);
}

// ------------------------------------------------------------------ ECDSA
struct Curve {
  int id;
  size_t len;
  const br_ec_impl* impl;
};
static Curve needCurve(VM& vm, Slot s) {
  std::string n = strUtf8(S(vm, s));
  for (auto& c : n) c = (char)toupper((unsigned char)c);
  if (n == "P-256" || n == "SECP256R1" || n == "PRIME256V1") return {BR_EC_secp256r1, 32, &br_ec_p256_m31};
  if (n == "P-384" || n == "SECP384R1") return {BR_EC_secp384r1, 48, &br_ec_prime_i31};
  if (n == "P-521" || n == "SECP521R1") return {BR_EC_secp521r1, 66, &br_ec_prime_i31};
  vm.error("unsupported curve '%s' (use P-256, P-384 or P-521)", n.c_str());
}
// raw scalar, SEC1 ECPrivateKey or PKCS#8 DER
static std::vector<uint8_t> ecPrivScalar(VM& vm, BytesObj* k, const Curve& c) {
  if (k->len == c.len) return vec(k);
  Der d{k->ptr(), k->len};
  size_t l;
  const uint8_t* seq = d.read(0x30, l);
  if (seq) {
    Der s{seq, l};
    size_t vl;
    const uint8_t* ver = s.read(0x02, vl);
    if (ver && vl == 1 && ver[0] == 0 && s.peek(0x30)) {
      size_t al;
      s.read(0x30, al);
      size_t ol;
      const uint8_t* oct = s.read(0x04, ol);
      if (oct) {
        Der inner{oct, ol};
        size_t il;
        const uint8_t* iseq = inner.read(0x30, il);
        if (iseq) {
          Der s2{iseq, il};
          s2.read(0x02, vl);
          size_t pl;
          const uint8_t* pk = s2.read(0x04, pl);
          if (pk && pl == c.len) return std::vector<uint8_t>(pk, pk + pl);
        }
      }
    } else if (ver && vl == 1 && ver[0] == 1) {
      size_t pl;
      const uint8_t* pk = s.read(0x04, pl);
      if (pk && pl == c.len) return std::vector<uint8_t>(pk, pk + pl);
    }
  }
  vm.error("invalid EC private key (expected %zu-byte scalar, SEC1 or PKCS#8 DER)", c.len);
}
// uncompressed point or SubjectPublicKeyInfo DER
static std::vector<uint8_t> ecPubPoint(BytesObj* k, const Curve& c, bool& ok) {
  ok = true;
  if (k->len == 2 * c.len + 1 && k->ptr()[0] == 4) return vec(k);
  Der d{k->ptr(), k->len};
  size_t l;
  const uint8_t* seq = d.read(0x30, l);
  if (seq) {
    Der s{seq, l};
    size_t al;
    s.read(0x30, al);
    size_t bl;
    const uint8_t* bits = s.read(0x03, bl);
    if (bits && bl == 2 * c.len + 2 && bits[0] == 0 && bits[1] == 4) return std::vector<uint8_t>(bits + 1, bits + bl);
  }
  ok = false;
  return {};
}
E7_NATIVE(c_ecdsaSign) {
  E7_UNUSED;
  Curve c = needCurve(vm, a[0]);
  HashInfo h = needHash(vm, a[1]);
  if (!h.br || h.name == "md5") vm.error("ECDSA requires a SHA-1/SHA-2 hash");
  std::vector<uint8_t> x = ecPrivScalar(vm, B(vm, a[2]), c);
  BytesObj* m = B(vm, a[3]);
  uint8_t hv[64];
  hashOnce(h, m->ptr(), m->len, hv);
  br_ec_private_key sk;
  sk.curve = c.id;
  sk.x = x.data();
  sk.xlen = x.size();
  uint8_t sig[132];
  size_t sl = br_ecdsa_i31_sign_raw(c.impl, h.br, hv, &sk, sig);
  crypto_wipe(x.data(), x.size());
  if (!sl) vm.error("ECDSA signing failed (invalid private key)");
  r->r = outBytes(vm, sig, sl);
}
E7_NATIVE(c_ecdsaVerify) {
  E7_UNUSED;
  Curve c = needCurve(vm, a[0]);
  HashInfo h = needHash(vm, a[1]);
  bool ok;
  std::vector<uint8_t> q = ecPubPoint(B(vm, a[2]), c, ok);
  BytesObj* m = B(vm, a[3]);
  BytesObj* sg = B(vm, a[4]);
  if (!ok) { r->i = 0; return; }
  std::vector<uint8_t> sig = vec(sg);
  if (sig.size() != 2 * c.len) {
    size_t rl = br_ecdsa_asn1_to_raw(sig.data(), sig.size());
    if (!rl || rl != 2 * c.len) { r->i = 0; return; }
    sig.resize(rl);
  }
  uint8_t hv[64];
  hashOnce(h, m->ptr(), m->len, hv);
  br_ec_public_key pk;
  pk.curve = c.id;
  pk.q = q.data();
  pk.qlen = q.size();
  r->i = br_ecdsa_i31_vrfy_raw(c.impl, hv, h.outLen, &pk, sig.data(), sig.size()) == 1;
}
E7_NATIVE(c_ecdsaPub) {
  E7_UNUSED;
  Curve c = needCurve(vm, a[0]);
  std::vector<uint8_t> x = ecPrivScalar(vm, B(vm, a[1]), c);
  br_ec_private_key sk;
  sk.curve = c.id;
  sk.x = x.data();
  sk.xlen = x.size();
  uint8_t buf[BR_EC_KBUF_PUB_MAX_SIZE];
  br_ec_public_key pk;
  size_t pn = br_ec_compute_pub(c.impl, &pk, buf, &sk);
  crypto_wipe(x.data(), x.size());
  if (!pn) vm.error("invalid EC private key");
  r->r = outBytes(vm, pk.q, pk.qlen);
}

// ------------------------------------------------------------------ RSA
static void mgf1(const HashInfo& h, const uint8_t* seed, size_t sl, uint8_t* mask, size_t ml) {
  uint8_t t[64];
  size_t off = 0;
  for (uint32_t c = 0; off < ml; c++) {
    uint8_t be[4] = {(uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c};
    hashParts(h, {{seed, sl}, {be, 4}}, t);
    size_t k = std::min(h.outLen, ml - off);
    memcpy(mask + off, t, k);
    off += k;
  }
}
static bool pssVerify(const HashInfo& h, const uint8_t* mHash, uint8_t* em, size_t emLen, uint32_t emBits) {
  size_t hl = h.outLen;
  if (emLen < hl + 2 || em[emLen - 1] != 0xBC) return false;
  size_t dbLen = emLen - hl - 1;
  uint8_t* db = em;
  const uint8_t* H = em + dbLen;
  unsigned topBits = 8 * emLen - emBits;
  if (topBits && (db[0] & (0xFF << (8 - topBits)))) return false;
  std::vector<uint8_t> mask(dbLen);
  mgf1(h, H, hl, mask.data(), dbLen);
  for (size_t i = 0; i < dbLen; i++) db[i] ^= mask[i];
  if (topBits) db[0] &= (uint8_t)(0xFF >> topBits);
  size_t i = 0;
  while (i < dbLen && db[i] == 0) i++;
  if (i == dbLen || db[i] != 1) return false;
  const uint8_t* salt = db + i + 1;
  size_t saltLen = dbLen - i - 1;
  uint8_t zeros[8] = {0};
  uint8_t h2[64];
  hashParts(h, {{zeros, 8}, {mHash, hl}, {salt, saltLen}}, h2);
  uint8_t acc = 0;
  for (size_t k = 0; k < hl; k++) acc |= h2[k] ^ H[k];
  return acc == 0;
}
static bool isPss(VM& vm, Slot s) {
  if (!s.r) return false;
  std::string p = strUtf8(S(vm, s));
  if (p == "pss") return true;
  if (p == "pkcs1" || p == "pkcs1v15" || p.empty()) return false;
  vm.error("unknown RSA padding '%s' (use \"pkcs1\" or \"pss\")", p.c_str());
}
E7_NATIVE(c_rsaVerify) {
  E7_UNUSED;
  HashInfo h = needHash(vm, a[0]);
  if (!h.oid) vm.error("RSA signatures require SHA-1 or SHA-2");
  BytesObj *kb = B(vm, a[1]), *m = B(vm, a[2]), *sg = B(vm, a[3]);
  bool pss = isPss(vm, a[4]);
  RsaPub pub;
  if (!parseRsaPub(kb->ptr(), kb->len, pub)) vm.error("invalid RSA public key (expected SPKI or PKCS#1 DER)");
  if (sg->len != pub.n.size()) { r->i = 0; return; }
  br_rsa_public_key pk;
  pk.n = pub.n.data();
  pk.nlen = pub.n.size();
  pk.e = pub.e.data();
  pk.elen = pub.e.size();
  uint8_t mh[64];
  hashOnce(h, m->ptr(), m->len, mh);
  if (!pss) {
    uint8_t out[64];
    if (!br_rsa_i31_pkcs1_vrfy(sg->ptr(), sg->len, h.oid, h.outLen, &pk, out)) { r->i = 0; return; }
    uint8_t acc = 0;
    for (size_t i = 0; i < h.outLen; i++) acc |= out[i] ^ mh[i];
    r->i = acc == 0;
    return;
  }
  std::vector<uint8_t> x = vec(sg);
  if (!br_rsa_i31_public(x.data(), x.size(), &pk)) { r->i = 0; return; }
  uint32_t modBits = bitLen(pub.n);
  uint32_t emBits = modBits - 1;
  size_t emLen = (emBits + 7) / 8;
  uint8_t* em = x.data() + (x.size() - emLen);
  if (x.size() > emLen && x[0] != 0) { r->i = 0; return; }
  r->i = pssVerify(h, mh, em, emLen, emBits);
}
E7_NATIVE(c_rsaSign) {
  E7_UNUSED;
  HashInfo h = needHash(vm, a[0]);
  if (!h.oid) vm.error("RSA signatures require SHA-1 or SHA-2");
  BytesObj *kb = B(vm, a[1]), *m = B(vm, a[2]);
  bool pss = isPss(vm, a[3]);
  RsaPriv pv;
  if (!parseRsaPriv(kb->ptr(), kb->len, pv)) vm.error("invalid RSA private key (expected PKCS#1 or PKCS#8 DER)");
  br_rsa_private_key sk;
  sk.n_bitlen = pv.bits;
  sk.p = pv.p.data(); sk.plen = pv.p.size();
  sk.q = pv.q.data(); sk.qlen = pv.q.size();
  sk.dp = pv.dp.data(); sk.dplen = pv.dp.size();
  sk.dq = pv.dq.data(); sk.dqlen = pv.dq.size();
  sk.iq = pv.iq.data(); sk.iqlen = pv.iq.size();
  size_t k = (pv.bits + 7) / 8;
  uint8_t mh[64];
  hashOnce(h, m->ptr(), m->len, mh);
  std::vector<uint8_t> x(k);
  if (!pss) {
    if (!br_rsa_i31_pkcs1_sign(h.oid, mh, h.outLen, &sk, x.data())) vm.error("RSA signing failed");
  } else {
    uint32_t emBits = pv.bits - 1;
    size_t emLen = (emBits + 7) / 8;
    size_t hl = h.outLen, sLen = hl;
    if (emLen < hl + sLen + 2) vm.error("RSA key too small for PSS");
    std::vector<uint8_t> salt(sLen);
    randomFill(vm, salt.data(), sLen);
    uint8_t zeros[8] = {0}, H[64];
    hashParts(h, {{zeros, 8}, {mh, hl}, {salt.data(), sLen}}, H);
    size_t dbLen = emLen - hl - 1;
    std::vector<uint8_t> db(dbLen, 0);
    db[dbLen - sLen - 1] = 1;
    memcpy(db.data() + dbLen - sLen, salt.data(), sLen);
    std::vector<uint8_t> mask(dbLen);
    mgf1(h, H, hl, mask.data(), dbLen);
    for (size_t i = 0; i < dbLen; i++) db[i] ^= mask[i];
    unsigned topBits = 8 * emLen - emBits;
    if (topBits) db[0] &= (uint8_t)(0xFF >> topBits);
    uint8_t* em = x.data() + (k - emLen);
    memcpy(em, db.data(), dbLen);
    memcpy(em + dbLen, H, hl);
    em[emLen - 1] = 0xBC;
    if (!br_rsa_i31_private(x.data(), &sk)) vm.error("RSA signing failed");
  }
  crypto_wipe(pv.d.data(), pv.d.size());
  crypto_wipe(pv.p.data(), pv.p.size());
  crypto_wipe(pv.q.data(), pv.q.size());
  r->r = outBytes(vm, x.data(), x.size());
}

void registerCryptoNatives(VM& vm) {
  registerEncodingNatives(vm);
  static const char* hashes[] = {"md5", "sha1", "sha224", "sha256", "sha384", "sha512",
                                 "sha3_224", "sha3_256", "sha3_384", "sha3_512", "keccak256"};
  for (const char* h : hashes) vm.registerNative(std::string("crypto.hash.") + h, "R>R", hashNative, (void*)h);
  vm.registerNative("crypto.hash", "RR>R", c_hash);
  vm.registerNative("crypto.hmac", "RRR>R", c_hmac);
  vm.registerNative("crypto.hmacKey", "RRR>R", c_hmacKey);
  vm.registerNative("crypto.hkdf", "RRRRF>R", c_hkdf);
  vm.registerNative("crypto.pbkdf2", "RRRFF>R", c_pbkdf2);
  vm.registerNative("crypto.crc32", "R>F", c_crc32);
  vm.registerNative("crypto.randomBytes", "F>R", c_randomBytes);
  vm.registerNative("crypto.timingSafeEqual", "RR>I", c_timingSafeEqual);
  vm.registerNative("crypto.aesGcmEncrypt", "RRRR>R", c_gcmEnc);
  vm.registerNative("crypto.aesGcmDecrypt", "RRRR>R", c_gcmDec);
  vm.registerNative("crypto.chacha20Poly1305Encrypt", "RRRR>R", c_chachaEnc);
  vm.registerNative("crypto.chacha20Poly1305Decrypt", "RRRR>R", c_chachaDec);
  vm.registerNative("crypto.aesCbcEncrypt", "RRR>R", c_cbcEnc);
  vm.registerNative("crypto.aesCbcDecrypt", "RRR>R", c_cbcDec);
  vm.registerNative("crypto.aesCtr", "RRR>R", c_ctr);
  vm.registerNative("crypto.ed25519PublicKey", "R>R", c_edPub);
  vm.registerNative("crypto.ed25519Sign", "RR>R", c_edSign);
  vm.registerNative("crypto.ed25519Verify", "RRR>I", c_edVerify);
  vm.registerNative("crypto.x25519", "RR>R", c_x25519);
  vm.registerNative("crypto.ecdsaSign", "RRRR>R", c_ecdsaSign);
  vm.registerNative("crypto.ecdsaVerify", "RRRRR>I", c_ecdsaVerify);
  vm.registerNative("crypto.ecdsaPublicKey", "RR>R", c_ecdsaPub);
  vm.registerNative("crypto.rsaVerify", "RRRRR>I", c_rsaVerify);
  vm.registerNative("crypto.rsaSign", "RRRR>R", c_rsaSign);
}

}  // namespace e7
