#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace e7 {
void sha512(const uint8_t* p, size_t n, uint8_t out[64]);
void sha256(const uint8_t* p, size_t n, uint8_t out[32]);
void ed25519PublicKey(const uint8_t seed[32], uint8_t pk[32]);
void ed25519Sign(const uint8_t seed[32], const uint8_t* msg, size_t n, uint8_t sig[64]);
bool ed25519Verify(const uint8_t pk[32], const uint8_t* msg, size_t n, const uint8_t sig[64]);
// Module signature trailer: "E7S1" | keyId[8] | sig[64]
constexpr size_t kSigTrailer = 76;
std::vector<uint8_t> signModuleBytes(const uint8_t* data, size_t len, const uint8_t seed[32]);
// Returns 1 if a valid trailer by a trusted key is present (payloadLen set), 0 if unsigned, -1 if invalid/untrusted.
int checkModuleSignature(const uint8_t* data, size_t len, const uint8_t* keys, size_t nkeys, size_t* payloadLen);
// Keccak / SHA-3 (keccak.cpp)
void sha3(const uint8_t* p, size_t n, uint8_t* out, size_t outLen, bool keccakPadding);
}  // namespace e7
