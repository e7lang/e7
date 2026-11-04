// Ed25519 (RFC 8032) built from Monocypher's EdDSA primitives + BearSSL SHA-512,
// and the e7b module signature format.
#include "sign.h"

#include <cstring>

extern "C" {
#include "bearssl_hash.h"
#include "monocypher.h"
}

namespace e7 {

void sha512(const uint8_t* p, size_t n, uint8_t out[64]) {
  br_sha512_context c;
  br_sha512_init(&c);
  br_sha512_update(&c, p, n);
  br_sha512_out(&c, out);
}
void sha256(const uint8_t* p, size_t n, uint8_t out[32]) {
  br_sha256_context c;
  br_sha256_init(&c);
  br_sha256_update(&c, p, n);
  br_sha256_out(&c, out);
}

static void expand(const uint8_t seed[32], uint8_t scalar[32], uint8_t prefix[32]) {
  uint8_t h[64];
  sha512(seed, 32, h);
  crypto_eddsa_trim_scalar(scalar, h);
  memcpy(prefix, h + 32, 32);
  crypto_wipe(h, sizeof h);
}

void ed25519PublicKey(const uint8_t seed[32], uint8_t pk[32]) {
  uint8_t s[32], prefix[32];
  expand(seed, s, prefix);
  crypto_eddsa_scalarbase(pk, s);
  crypto_wipe(s, 32);
  crypto_wipe(prefix, 32);
}

void ed25519Sign(const uint8_t seed[32], const uint8_t* msg, size_t n, uint8_t sig[64]) {
  uint8_t s[32], prefix[32], pk[32], h[64], r[32], k[32];
  expand(seed, s, prefix);
  crypto_eddsa_scalarbase(pk, s);
  br_sha512_context c;
  br_sha512_init(&c);
  br_sha512_update(&c, prefix, 32);
  br_sha512_update(&c, msg, n);
  br_sha512_out(&c, h);
  crypto_eddsa_reduce(r, h);
  crypto_eddsa_scalarbase(sig, r);  // R
  br_sha512_init(&c);
  br_sha512_update(&c, sig, 32);
  br_sha512_update(&c, pk, 32);
  br_sha512_update(&c, msg, n);
  br_sha512_out(&c, h);
  crypto_eddsa_reduce(k, h);
  crypto_eddsa_mul_add(sig + 32, k, s, r);  // S = k*s + r
  crypto_wipe(s, 32);
  crypto_wipe(prefix, 32);
  crypto_wipe(r, 32);
  crypto_wipe(h, 64);
}

bool ed25519Verify(const uint8_t pk[32], const uint8_t* msg, size_t n, const uint8_t sig[64]) {
  uint8_t h[64], k[32];
  br_sha512_context c;
  br_sha512_init(&c);
  br_sha512_update(&c, sig, 32);
  br_sha512_update(&c, pk, 32);
  br_sha512_update(&c, msg, n);
  br_sha512_out(&c, h);
  crypto_eddsa_reduce(k, h);
  return crypto_eddsa_check_equation(sig, pk, k) == 0;
}

static void keyId(const uint8_t pk[32], uint8_t id[8]) {
  uint8_t h[32];
  sha256(pk, 32, h);
  memcpy(id, h, 8);
}

std::vector<uint8_t> signModuleBytes(const uint8_t* data, size_t len, const uint8_t seed[32]) {
  std::vector<uint8_t> out(data, data + len);
  uint8_t pk[32], sig[64], id[8];
  ed25519PublicKey(seed, pk);
  keyId(pk, id);
  ed25519Sign(seed, data, len, sig);
  const char magic[4] = {'E', '7', 'S', '1'};
  out.insert(out.end(), magic, magic + 4);
  out.insert(out.end(), id, id + 8);
  out.insert(out.end(), sig, sig + 64);
  return out;
}

int checkModuleSignature(const uint8_t* data, size_t len, const uint8_t* keys, size_t nkeys, size_t* payloadLen) {
  *payloadLen = len;
  if (len < kSigTrailer + 8 || memcmp(data + len - kSigTrailer, "E7S1", 4) != 0) return 0;
  size_t plen = len - kSigTrailer;
  const uint8_t* id = data + plen + 4;
  const uint8_t* sig = data + plen + 12;
  for (size_t i = 0; i < nkeys; i++) {
    const uint8_t* pk = keys + 32 * i;
    uint8_t kid[8];
    keyId(pk, kid);
    if (memcmp(kid, id, 8) != 0) continue;
    if (ed25519Verify(pk, data, plen, sig)) {
      *payloadLen = plen;
      return 1;
    }
    return -1;
  }
  *payloadLen = plen;
  return -1;
}

}  // namespace e7
