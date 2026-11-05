// libFuzzer target: the natives of e7:crypto and e7:encoding, called through the C API with arguments
// taken from the input.
//
// A harness module (kHarness, compiled once) exports one wrapper per native (f_*) and properties
// (p_*: round trips, tamper detection, consistency between natives) that must return true whenever
// they complete. The input is a sequence of operations: one byte selects an exported function, the
// following bytes give its arguments according to its signature (byte strings, strings - raw or from
// a dictionary of hash / curve / padding / PEM label names -, numbers, booleans, key handles). Every
// call must end in success or a clean error; a property returning false aborts (a bug).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "e7/e7.h"

static const char kHarness[] = R"E7(
import { md5, sha1, sha224, sha256, sha384, sha512, sha3_224, sha3_256, sha3_384, sha3_512, keccak256, hash, hmac, hkdf,
  pbkdf2, crc32, randomBytes, timingSafeEqual, aesGcmEncrypt, aesGcmDecrypt, chacha20Poly1305Encrypt,
  chacha20Poly1305Decrypt, aesCbcEncrypt, aesCbcDecrypt, aesCtr, ed25519PublicKey, ed25519Sign, ed25519Verify, x25519,
  ecdsaSign, ecdsaVerify, ecdsaPublicKey, rsaVerify, rsaSign } from "e7:crypto";
import type { KeyHandle } from "e7:crypto";
import { hexEncode, hexDecode, base64Encode, base64Decode, base64UrlEncode, base64UrlDecode, base32Encode, base32Decode,
  utf8Encode, utf8Decode, pemDecode, pemEncode, concatBytes, bytesEqual } from "e7:encoding";

// RSA-2048 test key (tests/cases/lib/crypto_rsa_ecdsa.ts)
const RSA_SPKI = hexDecode(
  "30820122300d06092a864886f70d01010105000382010f003082010a0282010100b2078c8f1f035e2ceac5768f49e693c437" +
  "1ee8642d488d38b59287fc6c2d38edea91683dab7ad9a9b1413e35337f648dbc16d0a0f1afe311bdd3665cc115e422d3f56d" +
  "22aefb447d1b0f4203c8b4dd9461515cac7282de44a0ac160ac51144e3077371dddf6911e3018e433deb7dae0530980ca080" +
  "75a04c14457a2e5fa7143a783f6489b201a0e9359a3a1fa2b88c12ce256f8c6d39d49b2766f9c6f7ebc1b6532e852183924a" +
  "3149d045202f5beb1063f424c86bbef164c58f0218ed82b68a0067f65b0b8e4ed8745adcfdd702fb721bced005654e3b7e1b" +
  "82831f7167e626d1cd7d9d01b0169b0bd5b43f74ba156f1fabb4a45473dc82af2fc638e5ce4e5b0203010001");
const RSA_PKCS8 = hexDecode(
  "308204bc020100300d06092a864886f70d0101010500048204a6308204a20201000282010100b2078c8f1f035e2ceac5768f" +
  "49e693c4371ee8642d488d38b59287fc6c2d38edea91683dab7ad9a9b1413e35337f648dbc16d0a0f1afe311bdd3665cc115" +
  "e422d3f56d22aefb447d1b0f4203c8b4dd9461515cac7282de44a0ac160ac51144e3077371dddf6911e3018e433deb7dae05" +
  "30980ca08075a04c14457a2e5fa7143a783f6489b201a0e9359a3a1fa2b88c12ce256f8c6d39d49b2766f9c6f7ebc1b6532e" +
  "852183924a3149d045202f5beb1063f424c86bbef164c58f0218ed82b68a0067f65b0b8e4ed8745adcfdd702fb721bced005" +
  "654e3b7e1b82831f7167e626d1cd7d9d01b0169b0bd5b43f74ba156f1fabb4a45473dc82af2fc638e5ce4e5b020301000102" +
  "8201002de460496c9733a9a4836e82956c48df25ce63dd9f06bad0c98d925c718e5ec485d921e3f3d66151ab0dc4797662fa" +
  "8ac6d7a2c43a493ae191eb72d1aa3c7868b9f6e4413296371b80e438023db4d201b3ec835538d79f705572c6e3d3d515cae8" +
  "5acc1983c3d4068e9a1b9f549b3b46bf3612db35021d663f3b81b97e92d555a6caf89884ceb08db100a73a1a99321b702b53" +
  "206ef1d574a098009dae0e3053452452c18dc2c169c5e0904247b3b67cbcb9ca625a6d4f801d9af2e40379973116df98cc47" +
  "50eea213a969f0748e7aa4864c742ecfe11a6d6980a3aaa937f986c88bf5c12b569c5725883b01b6b5eef62ff0bc8d825dc7" +
  "50fd0454bf38dc493102818100e7432441334e3ae7cacc46faec608698a15b4162f80f1dd92bbb9b75c546d6fa2d1accce91" +
  "1c37fb0378d90bf1f3164440b509a466cda5137a8b02249c3cb10c3b892f4c406d995a0812c2c147483365af72e808728b67" +
  "20df7383a490cb16495f8fd4349258dd339b84fef2e99042bee07499e5eb575fd75edf90617c9eb8d702818100c512aff0c4" +
  "2cf00be1350ca66d6048cf17049f4518a2feab19d0a0927f983e35240d7a78567024c06c4f8295874c1140966f184a29bd6a" +
  "0e9a0a05f8864929c9ceadde1f7d9d8aed954b522f3fd3c4277148bec07c52f6929347be9aa7d6afb2be3d14d3ea7ad6e184" +
  "0625502785435619f454de3fb9258e6d2cefdec805d21d0281802df7ffa5e22fe3af9d751b8ad71f9cc18c195a96f49df49d" +
  "ed7e7e395bbc68111494262fcd5261a2489f65ed64eb9ffa732e493b5f9ff2f73b3d94707889ebdb759e2043b4eed433bc35" +
  "16775ad6f4387b3688fedcddeee2eee7c43c1af7f3772442afb8bf7a6085d89d081c7ae6cd0daa0a23693b6d83c488bc41b3" +
  "cf14b1c30281801051bad61238b26c196b1e300df0a236dbee1b0e4abafca1c8927929887d5ff16099e9215bef0f6c0bcc0d" +
  "c13a21f76fc4c5505a9bd83e9714f46d8825fe9b02462629b5abde46e9e3aa08153790deb6717017b3b2d7dfe9455ef48eb4" +
  "055829520d96d2f1a0d878764957ffaa3623dc566be03743499ead8ba6e3fa2bd027950281805a443a4afa37d7371a1d56bb" +
  "40801c0846f30d27785c5dacac267263bb16fd66f5f962f6dbe0c2d6f2d12cbe9b184b2bf0b443d61dee294e147a81c39851" +
  "70583f8175ae1d245fc1db1699c3278d2983033b8525af4dd01aed052c7947a9f7dc6618d56ffcb249532775e00d93816fcb" +
  "f845745ae2f25971836344e05bc78063");

function opt(use: boolean, b: Uint8Array): Uint8Array | null { return use ? b : null; }
function optStr(use: boolean, s: string): string | null { return use ? s : null; }
function digest(which: number, d: Uint8Array): Uint8Array {
  switch (which % 11) {
    case 0: return md5(d);
    case 1: return sha1(d);
    case 2: return sha224(d);
    case 3: return sha256(d);
    case 4: return sha384(d);
    case 5: return sha512(d);
    case 6: return sha3_224(d);
    case 7: return sha3_256(d);
    case 8: return sha3_384(d);
    case 9: return sha3_512(d);
    default: return keccak256(d);
  }
}
const HASH_NAMES: string[] = ["md5", "sha1", "sha224", "sha256", "sha384", "sha512", "sha3-224", "sha3-256", "sha3-384",
  "sha3-512", "keccak256"];
function flipBit(b: Uint8Array, at: number): void {
  const i = Math.floor(at) % b.length;
  b[i] = b[i] ^ (1 << (Math.floor(at) % 8));
}

// ---- one wrapper per native
export function f_hexEncode(d: Uint8Array): string { return hexEncode(d); }
export function f_hexDecode(s: string): Uint8Array { return hexDecode(s); }
export function f_base64Encode(d: Uint8Array): string { return base64Encode(d); }
export function f_base64Decode(s: string): Uint8Array { return base64Decode(s); }
export function f_base64UrlEncode(d: Uint8Array): string { return base64UrlEncode(d); }
export function f_base64UrlDecode(s: string): Uint8Array { return base64UrlDecode(s); }
export function f_base32Encode(d: Uint8Array): string { return base32Encode(d); }
export function f_base32Decode(s: string): Uint8Array { return base32Decode(s); }
export function f_utf8Encode(s: string): Uint8Array { return utf8Encode(s); }
export function f_utf8Decode(d: Uint8Array): string { return utf8Decode(d); }
export function f_pemDecode(s: string, label: string, useLabel: boolean): Uint8Array { return pemDecode(s, optStr(useLabel, label)); }
export function f_pemEncode(d: Uint8Array, label: string): string { return pemEncode(d, label); }
export function f_concatBytes(a: Uint8Array, b: Uint8Array, c: Uint8Array): Uint8Array { return concatBytes(a, b, c); }
export function f_bytesEqual(a: Uint8Array, b: Uint8Array): boolean { return bytesEqual(a, b); }
export function f_digest(which: number, d: Uint8Array): Uint8Array { return digest(which, d); }
export function f_hash(alg: string, d: Uint8Array): Uint8Array { return hash(alg, d); }
export function f_hmac(alg: string, k: Uint8Array, d: Uint8Array): Uint8Array { return hmac(alg, k, d); }
export function f_hmacKey(alg: string, k: KeyHandle, d: Uint8Array): Uint8Array { return hmac(alg, k, d); }
export function f_hkdf(alg: string, ikm: Uint8Array, salt: Uint8Array, info: Uint8Array, n: number): Uint8Array { return hkdf(alg, ikm, salt, info, n); }
export function f_pbkdf2(alg: string, pw: Uint8Array, salt: Uint8Array, it: number, n: number): Uint8Array { return pbkdf2(alg, pw, salt, it, n); }
export function f_crc32(d: Uint8Array): number { return crc32(d); }
export function f_randomBytes(n: number): Uint8Array { return randomBytes(n); }
export function f_timingSafeEqual(a: Uint8Array, b: Uint8Array): boolean { return timingSafeEqual(a, b); }
export function f_aesGcmEncrypt(k: Uint8Array, iv: Uint8Array, p: Uint8Array, aad: Uint8Array, useAad: boolean): Uint8Array { return aesGcmEncrypt(k, iv, p, opt(useAad, aad)); }
export function f_aesGcmDecrypt(k: Uint8Array, iv: Uint8Array, c: Uint8Array, aad: Uint8Array, useAad: boolean): boolean { return aesGcmDecrypt(k, iv, c, opt(useAad, aad)) !== null; }
export function f_chachaEncrypt(k: Uint8Array, iv: Uint8Array, p: Uint8Array, aad: Uint8Array, useAad: boolean): Uint8Array { return chacha20Poly1305Encrypt(k, iv, p, opt(useAad, aad)); }
export function f_chachaDecrypt(k: Uint8Array, iv: Uint8Array, c: Uint8Array, aad: Uint8Array, useAad: boolean): boolean { return chacha20Poly1305Decrypt(k, iv, c, opt(useAad, aad)) !== null; }
export function f_aesCbcEncrypt(k: Uint8Array, iv: Uint8Array, p: Uint8Array): Uint8Array { return aesCbcEncrypt(k, iv, p); }
export function f_aesCbcDecrypt(k: Uint8Array, iv: Uint8Array, c: Uint8Array): boolean { return aesCbcDecrypt(k, iv, c) !== null; }
export function f_aesCtr(k: Uint8Array, ctr: Uint8Array, d: Uint8Array): Uint8Array { return aesCtr(k, ctr, d); }
export function f_ed25519PublicKey(seed: Uint8Array): Uint8Array { return ed25519PublicKey(seed); }
export function f_ed25519Sign(seed: Uint8Array, m: Uint8Array): Uint8Array { return ed25519Sign(seed, m); }
export function f_ed25519Verify(pub: Uint8Array, m: Uint8Array, sig: Uint8Array): boolean { return ed25519Verify(pub, m, sig); }
export function f_x25519(a: Uint8Array, b: Uint8Array): Uint8Array { return x25519(a, b); }
export function f_ecdsaSign(curve: string, alg: string, priv: Uint8Array, m: Uint8Array): Uint8Array { return ecdsaSign(curve, alg, priv, m); }
export function f_ecdsaVerify(curve: string, alg: string, pub: Uint8Array, m: Uint8Array, sig: Uint8Array): boolean { return ecdsaVerify(curve, alg, pub, m, sig); }
export function f_ecdsaPublicKey(curve: string, priv: Uint8Array): Uint8Array { return ecdsaPublicKey(curve, priv); }
export function f_rsaVerify(alg: string, pub: Uint8Array, m: Uint8Array, sig: Uint8Array, pad: string, usePad: boolean): boolean { return rsaVerify(alg, pub, m, sig, optStr(usePad, pad)); }
export function f_rsaSign(alg: string, priv: Uint8Array, m: Uint8Array, pad: string, usePad: boolean): Uint8Array { return rsaSign(alg, priv, m, optStr(usePad, pad)); }

)E7" /* split: MSVC limits a single string literal to 16 KB */ R"E7(
// ---- properties (must return true whenever they complete)
export function p_encoding(d: Uint8Array): boolean {
  if (!bytesEqual(hexDecode(hexEncode(d)), d)) return false;
  if (!bytesEqual(base64Decode(base64Encode(d)), d)) return false;
  if (!bytesEqual(base64UrlDecode(base64UrlEncode(d)), d)) return false;
  if (!bytesEqual(base32Decode(base32Encode(d)), d)) return false;
  if (hexEncode(d).length !== d.length * 2) return false;
  const s = utf8Decode(d);
  if (utf8Decode(utf8Encode(s)) !== s) return false;
  const two = concatBytes(d, d);
  if (two.length !== 2 * d.length || !bytesEqual(two.subarray(d.length), d)) return false;
  return timingSafeEqual(d, d) && bytesEqual(d, d);
}
export function p_pem(d: Uint8Array, label: string): boolean { return bytesEqual(pemDecode(pemEncode(d, label), label), d); }
export function p_hash(d: Uint8Array): boolean {
  for (let i = 0; i < HASH_NAMES.length; i++) if (!bytesEqual(hash(HASH_NAMES[i], d), digest(i, d))) return false;
  return true;
}
export function p_hmacKey(alg: string, h: KeyHandle, key: Uint8Array, d: Uint8Array): boolean { return bytesEqual(hmac(alg, h, d), hmac(alg, key, d)); }
export function p_kdfPrefix(alg: string, a: Uint8Array, b: Uint8Array, c: Uint8Array, n: number, it: number): boolean {
  const len = Math.floor(n) % 200 + 1;
  const h = hkdf(alg, a, b, c, len), h2 = hkdf(alg, a, b, c, len + 7);
  if (h.length !== len || !bytesEqual(h2.subarray(0, len), h)) return false;
  const rounds = Math.floor(it) % 50 + 1;
  const p = pbkdf2(alg, a, b, rounds, len), p2 = pbkdf2(alg, a, b, rounds, len + 33);
  return p.length === len && bytesEqual(p2.subarray(0, len), p);
}
export function p_aead(chacha: boolean, k: Uint8Array, iv: Uint8Array, p: Uint8Array, aad: Uint8Array, useAad: boolean, flip: number): boolean {
  const a = opt(useAad, aad);
  const c = chacha ? chacha20Poly1305Encrypt(k, iv, p, a) : aesGcmEncrypt(k, iv, p, a);
  if (c.length !== p.length + 16) return false;
  const d = chacha ? chacha20Poly1305Decrypt(k, iv, c, a) : aesGcmDecrypt(k, iv, c, a);
  if (d === null || !bytesEqual(d, p)) return false;
  flipBit(c, flip);
  return (chacha ? chacha20Poly1305Decrypt(k, iv, c, a) : aesGcmDecrypt(k, iv, c, a)) === null;
}
export function p_cbc(k: Uint8Array, iv: Uint8Array, p: Uint8Array): boolean {
  const c = aesCbcEncrypt(k, iv, p);
  if (c.length % 16 !== 0 || c.length <= p.length) return false;
  const d = aesCbcDecrypt(k, iv, c);
  return d !== null && bytesEqual(d, p);
}
export function p_ctr(k: Uint8Array, ctr: Uint8Array, p: Uint8Array): boolean {
  const c = aesCtr(k, ctr, p);
  return c.length === p.length && bytesEqual(aesCtr(k, ctr, c), p);
}
export function p_ed25519(seed: Uint8Array, m: Uint8Array, flip: number): boolean {
  const pub = ed25519PublicKey(seed), sig = ed25519Sign(seed, m);
  if (!ed25519Verify(pub, m, sig)) return false;
  flipBit(sig, flip);
  return !ed25519Verify(pub, m, sig);
}
export function p_x25519(a: Uint8Array, b: Uint8Array): boolean {
  const base = new Uint8Array(32);
  base[0] = 9;
  return bytesEqual(x25519(a, x25519(b, base)), x25519(b, x25519(a, base)));
}
export function p_ecdsa(curve: string, alg: string, priv: Uint8Array, m: Uint8Array, flip: number): boolean {
  const pub = ecdsaPublicKey(curve, priv), sig = ecdsaSign(curve, alg, priv, m);
  if (!ecdsaVerify(curve, alg, pub, m, sig)) return false;
  flipBit(sig, flip);
  return !ecdsaVerify(curve, alg, pub, m, sig);
}
export function p_rsa(alg: string, m: Uint8Array, pad: string, usePad: boolean, flip: number): boolean {
  const sig = rsaSign(alg, RSA_PKCS8, m, optStr(usePad, pad));
  if (!rsaVerify(alg, RSA_SPKI, m, sig, optStr(usePad, pad))) return false;
  flipBit(sig, flip);
  return !rsaVerify(alg, RSA_SPKI, m, sig, optStr(usePad, pad));
}
)E7";

// Argument kinds: y bytes, k bytes that are also the key behind handle 1, s raw string, a hash name,
// c curve name, p RSA padding, l PEM label, n number, f small non-negative integer, b boolean,
// h key handle (1 resolves to the last 'k' bytes, other ids are unknown keys).
struct Op {
  const char* name;
  const char* args;
};
static const Op kOps[] = {
    {"f_hexEncode", "y"}, {"f_hexDecode", "s"}, {"f_base64Encode", "y"}, {"f_base64Decode", "s"},
    {"f_base64UrlEncode", "y"}, {"f_base64UrlDecode", "s"}, {"f_base32Encode", "y"}, {"f_base32Decode", "s"},
    {"f_utf8Encode", "s"}, {"f_utf8Decode", "y"}, {"f_pemDecode", "slb"}, {"f_pemEncode", "yl"},
    {"f_concatBytes", "yyy"}, {"f_bytesEqual", "yy"}, {"f_digest", "fy"}, {"f_hash", "ay"}, {"f_hmac", "ayy"},
    {"f_hmacKey", "ahy"}, {"f_hkdf", "ayyyn"}, {"f_pbkdf2", "ayynn"}, {"f_crc32", "y"}, {"f_randomBytes", "n"},
    {"f_timingSafeEqual", "yy"}, {"f_aesGcmEncrypt", "yyyyb"}, {"f_aesGcmDecrypt", "yyyyb"},
    {"f_chachaEncrypt", "yyyyb"}, {"f_chachaDecrypt", "yyyyb"}, {"f_aesCbcEncrypt", "yyy"},
    {"f_aesCbcDecrypt", "yyy"}, {"f_aesCtr", "yyy"}, {"f_ed25519PublicKey", "y"}, {"f_ed25519Sign", "yy"},
    {"f_ed25519Verify", "yyy"}, {"f_x25519", "yy"}, {"f_ecdsaSign", "cayy"}, {"f_ecdsaVerify", "cayyy"},
    {"f_ecdsaPublicKey", "cy"}, {"f_rsaVerify", "ayyypb"}, {"f_rsaSign", "ayypb"},
    {"p_encoding", "y"}, {"p_pem", "yl"}, {"p_hash", "y"}, {"p_hmacKey", "ahky"}, {"p_kdfPrefix", "ayyyff"},
    {"p_aead", "byyyybf"}, {"p_cbc", "yyy"}, {"p_ctr", "yyy"}, {"p_ed25519", "yyf"}, {"p_x25519", "yy"},
    {"p_ecdsa", "cayyf"}, {"p_rsa", "aypbf"},
};
static const size_t kNumOps = sizeof kOps / sizeof kOps[0];

static const char* const kHashNames[] = {"md5", "sha1", "sha224", "sha256", "sha384", "sha512", "sha3-224",
                                         "sha3-256", "sha3-384", "sha3-512", "keccak256", "SHA-256", "SHA-1",
                                         "SHA-384", "SHA-512", "sha2", ""};
static const char* const kCurves[] = {"P-256", "P-384", "P-521", "p-256", "secp256k1"};
static const char* const kPaddings[] = {"pkcs1", "pss", "PSS", "oaep"};
static const char* const kLabels[] = {"CERTIFICATE", "PUBLIC KEY", "PRIVATE KEY", "EC PRIVATE KEY",
                                      "RSA PRIVATE KEY", "X"};
#define DICT(d) d, sizeof d / sizeof d[0]

struct Reader {
  const uint8_t* p;
  size_t n;
  uint8_t byte() {
    if (!n) return 0;
    n--;
    return *p++;
  }
  // a length byte (>= 240: a second byte extends it, up to 4095), then that many bytes
  std::vector<uint8_t> bytes() {
    size_t len = byte();
    if (len >= 240) len = (len - 240) * 256 + byte();
    if (len > n) len = n;
    std::vector<uint8_t> v(p, p + len);
    p += len;
    n -= len;
    return v;
  }
  std::string str(const char* const* dict, size_t dictN) {
    uint8_t b = byte();
    if (dict && b < 224) return dict[b % dictN];
    std::vector<uint8_t> v = bytes();
    return std::string(v.begin(), v.end());
  }
  double number() {
    uint8_t b = byte();
    if (b < 160) return b;
    if (b < 224) return byte() * 256.0 + byte();
    uint8_t raw[8];
    for (uint8_t& r : raw) r = byte();
    double d;
    memcpy(&d, raw, 8);  // any double: negative, huge, fractional, NaN, infinity
    return d;
  }
};

static std::vector<uint8_t> gKey;  // the key bytes behind handle 1
static int keyProvider(void*, int64_t id, const uint8_t** key, size_t* len) {
  if (id != 1) return 1;
  *key = gKey.data();
  *len = gKey.size();
  return 0;
}
static void quiet(void*, const char*, size_t) {}

static const std::vector<uint8_t>& harness() {
  static const std::vector<uint8_t> bc = [] {
    uint8_t* out = nullptr;
    size_t len = 0;
    char* errors = nullptr;
    if (e7_compile(kHarness, sizeof kHarness - 1, "fuzz_natives.ts", &out, &len, &errors) != E7_OK) {
      fprintf(stderr, "fuzz_natives: the harness does not compile:\n%s\n", errors ? errors : "");
      abort();
    }
    std::vector<uint8_t> v(out, out + len);
    e7_free(out);
    return v;
  }();
  return bc;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::vector<uint8_t>& bc = harness();
  e7_config cfg;
  e7_config_init(&cfg);
  cfg.memory_limit = 32u << 20;
  cfg.max_frames = 200;
  e7_vm* vm = e7_vm_new(&cfg);
  if (!vm) return 0;
  e7_set_print(vm, quiet, nullptr);
  e7_set_key_provider(vm, keyProvider, nullptr);
  e7_seed_random(vm, 7);
  e7_module* mod = nullptr;
  if (e7_load(vm, bc.data(), bc.size(), &mod) != E7_OK) {
    fprintf(stderr, "fuzz_natives: the harness does not load: %s\n", e7_last_error(vm));
    abort();
  }
  Reader in{data, size};
  gKey.clear();
  for (int step = 0; step < 8 && in.n > 0; step++) {
    const Op& op = kOps[in.byte() % kNumOps];
    std::deque<std::vector<uint8_t>> keep;  // argument storage (stable addresses)
    std::vector<e7_value> args;
    for (const char* a = op.args; *a; a++) {
      e7_value v;
      memset(&v, 0, sizeof v);
      switch (*a) {
        case 'y':
        case 'k':
          keep.push_back(in.bytes());
          if (*a == 'k') gKey = keep.back();
          v.type = E7_BYTES;
          v.v.bytes.ptr = keep.back().data();
          v.v.bytes.len = keep.back().size();
          break;
        case 's':
        case 'a':
        case 'c':
        case 'p':
        case 'l': {
          std::string s = *a == 'a'   ? in.str(DICT(kHashNames))
                          : *a == 'c' ? in.str(DICT(kCurves))
                          : *a == 'p' ? in.str(DICT(kPaddings))
                          : *a == 'l' ? in.str(DICT(kLabels))
                                      : in.str(nullptr, 0);
          keep.emplace_back(s.begin(), s.end());
          v.type = E7_STRING;
          v.v.str.ptr = (const char*)keep.back().data();
          v.v.str.len = keep.back().size();
          break;
        }
        case 'n': v.type = E7_NUMBER; v.v.num = in.number(); break;
        case 'f': v.type = E7_NUMBER; v.v.num = in.byte(); break;
        case 'b': v.type = E7_BOOL; v.v.boolean = in.byte() & 1; break;
        case 'h': v.type = E7_HANDLE; v.v.handle = in.byte() % 3; break;
      }
      args.push_back(v);
    }
    e7_set_budget(vm, 200000);
    e7_value ret;
    e7_status st = e7_call(vm, mod, op.name, args.data(), args.size(), &ret);
    static const bool trace = getenv("E7_FUZZ_TRACE") != nullptr;  // which calls succeed (seed tuning)
    if (trace) fprintf(stderr, "%-20s %d %s\n", op.name, (int)st, st == E7_OK ? "" : e7_last_error(vm));
    if (st == E7_ERR_ARGS || st == E7_ERR_NOT_FOUND) {  // a harness bug, not a finding
      fprintf(stderr, "fuzz_natives: %s: %s\n", op.name, e7_last_error(vm));
      abort();
    }
    if (st == E7_OK && op.name[0] == 'p' && !(ret.type == E7_BOOL && ret.v.boolean)) {
      fprintf(stderr, "fuzz_natives: property %s does not hold\n", op.name);
      abort();
    }
  }
  e7_vm_free(vm);
  return 0;
}
