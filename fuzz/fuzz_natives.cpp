// libFuzzer target: the natives of e7:crypto and e7:encoding, called through the C API with arguments
// taken from the input.
//
// A harness module (kHarness, compiled once) exports one wrapper per native (f_*) and properties
// (p_*: round trips, tamper detection, consistency between natives) that must return true whenever
// they complete. The input is a sequence of operations: one byte selects an exported function, the
// following bytes give its arguments according to its signature (byte strings, strings - raw or from
// a dictionary of hash / PEM label names -, numbers, booleans, key handles). Every
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
import { sha256, sha384, sha512, hash, hmac, hkdf, pbkdf2, randomBytes, timingSafeEqual, aesGcmEncrypt, aesGcmDecrypt,
  chacha20Poly1305Encrypt, chacha20Poly1305Decrypt, ed25519PublicKey, ed25519Sign, ed25519Verify, x25519 } from "e7:crypto";
import type { KeyHandle } from "e7:crypto";
import { hexEncode, hexDecode, base64Encode, base64Decode, base64UrlEncode, base64UrlDecode, base32Encode, base32Decode,
  utf8Encode, utf8Decode, pemDecode, pemEncode, concatBytes, bytesEqual, crc32 } from "e7:encoding";

function opt(use: boolean, b: Uint8Array): Uint8Array | null { return use ? b : null; }
function optStr(use: boolean, s: string): string | null { return use ? s : null; }
function digest(which: number, d: Uint8Array): Uint8Array {
  switch (which % 3) {
    case 0: return sha256(d);
    case 1: return sha384(d);
    default: return sha512(d);
  }
}
const HASH_NAMES: string[] = ["sha256", "sha384", "sha512"];
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
export function f_ed25519PublicKey(seed: Uint8Array): Uint8Array { return ed25519PublicKey(seed); }
export function f_ed25519Sign(seed: Uint8Array, m: Uint8Array): Uint8Array { return ed25519Sign(seed, m); }
export function f_ed25519Verify(pub: Uint8Array, m: Uint8Array, sig: Uint8Array): boolean { return ed25519Verify(pub, m, sig); }
export function f_x25519(a: Uint8Array, b: Uint8Array): Uint8Array { return x25519(a, b); }

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
)E7";

// Argument kinds: y bytes, k bytes that are also the key behind handle 1, s raw string, a hash name,
// l PEM label, n number, f small non-negative integer, b boolean,
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
    {"f_chachaEncrypt", "yyyyb"}, {"f_chachaDecrypt", "yyyyb"}, {"f_ed25519PublicKey", "y"},
    {"f_ed25519Sign", "yy"}, {"f_ed25519Verify", "yyy"}, {"f_x25519", "yy"},
    {"p_encoding", "y"}, {"p_pem", "yl"}, {"p_hash", "y"}, {"p_hmacKey", "ahky"}, {"p_kdfPrefix", "ayyyff"},
    {"p_aead", "byyyybf"}, {"p_ed25519", "yyf"}, {"p_x25519", "yy"},
};
static const size_t kNumOps = sizeof kOps / sizeof kOps[0];

// the supported names first, then other spellings and names that must be rejected cleanly
static const char* const kHashNames[] = {"sha256", "sha384", "sha512", "SHA-256", "SHA-384", "SHA-512", "sha_256",
                                         "sha1", "md5", "sha3-256", "sha2", ""};
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
        case 'l': {
          std::string s = *a == 'a'   ? in.str(DICT(kHashNames))
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
