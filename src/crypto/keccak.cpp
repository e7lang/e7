// Keccak-f[1600] sponge: SHA3-224/256/384/512 (FIPS 202) and Keccak-256 (original padding).
#include <cstring>

#include "sign.h"

namespace e7 {

static const uint64_t RC[24] = {
    0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808Aull, 0x8000000080008000ull,
    0x000000000000808Bull, 0x0000000080000001ull, 0x8000000080008081ull, 0x8000000000008009ull,
    0x000000000000008Aull, 0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000Aull,
    0x000000008000808Bull, 0x800000000000008Bull, 0x8000000000008089ull, 0x8000000000008003ull,
    0x8000000000008002ull, 0x8000000000000080ull, 0x000000000000800Aull, 0x800000008000000Aull,
    0x8000000080008081ull, 0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull};
static const int ROT[25] = {0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43, 25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14};

static inline uint64_t rol(uint64_t x, int n) { return n ? (x << n) | (x >> (64 - n)) : x; }

static void keccakF(uint64_t s[25]) {
  for (int round = 0; round < 24; round++) {
    uint64_t C[5], D[5], B[25];
    for (int x = 0; x < 5; x++) C[x] = s[x] ^ s[x + 5] ^ s[x + 10] ^ s[x + 15] ^ s[x + 20];
    for (int x = 0; x < 5; x++) D[x] = C[(x + 4) % 5] ^ rol(C[(x + 1) % 5], 1);
    for (int i = 0; i < 25; i++) s[i] ^= D[i % 5];
    for (int x = 0; x < 5; x++)
      for (int y = 0; y < 5; y++) B[y + 5 * ((2 * x + 3 * y) % 5)] = rol(s[x + 5 * y], ROT[x + 5 * y]);
    for (int x = 0; x < 5; x++)
      for (int y = 0; y < 5; y++) s[x + 5 * y] = B[x + 5 * y] ^ (~B[(x + 1) % 5 + 5 * y] & B[(x + 2) % 5 + 5 * y]);
    s[0] ^= RC[round];
  }
}

void sha3(const uint8_t* p, size_t n, uint8_t* out, size_t outLen, bool keccakPadding) {
  uint64_t s[25];
  memset(s, 0, sizeof s);
  size_t rate = 200 - 2 * outLen;
  uint8_t block[200];
  while (n >= rate) {
    for (size_t i = 0; i < rate / 8; i++) {
      uint64_t w = 0;
      for (int b = 0; b < 8; b++) w |= (uint64_t)p[8 * i + b] << (8 * b);
      s[i] ^= w;
    }
    keccakF(s);
    p += rate;
    n -= rate;
  }
  memset(block, 0, rate);
  if (n) memcpy(block, p, n);
  block[n] ^= keccakPadding ? 0x01 : 0x06;
  block[rate - 1] ^= 0x80;
  for (size_t i = 0; i < rate / 8; i++) {
    uint64_t w = 0;
    for (int b = 0; b < 8; b++) w |= (uint64_t)block[8 * i + b] << (8 * b);
    s[i] ^= w;
  }
  keccakF(s);
  for (size_t i = 0; i < outLen; i++) out[i] = (uint8_t)(s[i / 8] >> (8 * (i % 8)));
}

}  // namespace e7
