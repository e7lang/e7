// e7:encoding natives: hex, base64, base64url, base32, utf8, PEM, CRC-32, byte helpers.
#include "util.h"

namespace e7 {

uint32_t crc32(const uint8_t* p, size_t n);  // core/serialize.cpp

static std::u16string a16(const std::string& s) { return std::u16string(s.begin(), s.end()); }
static std::string asciiOf(VM& vm, StrObj* s) {
  std::string out;
  out.reserve(s->len);
  for (uint32_t i = 0; i < s->len; i++) {
    if (s->chars()[i] > 127) vm.error("invalid character in encoded input");
    out.push_back((char)s->chars()[i]);
  }
  return out;
}

E7_NATIVE(e_hexEncode) {
  E7_UNUSED;
  BytesObj* b = B(vm, a[0]);
  static const char* hx = "0123456789abcdef";
  StrObj* s = vm.allocString((size_t)b->len * 2);
  for (uint32_t i = 0; i < b->len; i++) {
    s->chars()[2 * i] = hx[b->ptr()[i] >> 4];
    s->chars()[2 * i + 1] = hx[b->ptr()[i] & 15];
  }
  r->r = s;
}
E7_NATIVE(e_hexDecode) {
  E7_UNUSED;
  std::string s = asciiOf(vm, S(vm, a[0]));
  if (s.size() % 2) vm.error("hexDecode: odd-length input");
  auto hv = [&](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    vm.error("hexDecode: invalid hex character");
  };
  BytesObj* b = vm.newBytes((uint32_t)(s.size() / 2));
  for (size_t i = 0; i < s.size() / 2; i++) b->ptr()[i] = (uint8_t)(hv(s[2 * i]) * 16 + hv(s[2 * i + 1]));
  r->r = b;
}

static std::string b64enc(const uint8_t* p, size_t n, bool url) {
  const char* tbl = url ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
                        : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((n + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < n; i += 3) {
    uint32_t v = (p[i] << 16) | (p[i + 1] << 8) | p[i + 2];
    out += tbl[v >> 18];
    out += tbl[(v >> 12) & 63];
    out += tbl[(v >> 6) & 63];
    out += tbl[v & 63];
  }
  if (i < n) {
    uint32_t v = p[i] << 16;
    if (i + 1 < n) v |= p[i + 1] << 8;
    out += tbl[v >> 18];
    out += tbl[(v >> 12) & 63];
    if (i + 1 < n) out += tbl[(v >> 6) & 63];
    else if (!url) out += '=';
    if (!url) out += '=';
  }
  return out;
}
// accepts both alphabets, optional padding, ignores whitespace
static bool b64dec(const std::string& in, std::vector<uint8_t>& out) {
  uint32_t acc = 0;
  int bits = 0;
  size_t pad = 0, count = 0;
  for (char c : in) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    if (c == '=') { pad++; continue; }
    if (pad) return false;
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+' || c == '-') v = 62;
    else if (c == '/' || c == '_') v = 63;
    else return false;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    count++;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((uint8_t)(acc >> bits));
    }
  }
  if (count % 4 == 1) return false;
  if (pad > 2 || (pad && (count + pad) % 4 != 0)) return false;
  return true;
}
E7_NATIVE(e_b64Encode) { E7_UNUSED; BytesObj* b = B(vm, a[0]); r->r = vm.newString(a16(b64enc(b->ptr(), b->len, false))); }
E7_NATIVE(e_b64UrlEncode) { E7_UNUSED; BytesObj* b = B(vm, a[0]); r->r = vm.newString(a16(b64enc(b->ptr(), b->len, true))); }
E7_NATIVE(e_b64Decode) {
  E7_UNUSED;
  std::vector<uint8_t> out;
  if (!b64dec(asciiOf(vm, S(vm, a[0])), out)) vm.error("base64Decode: invalid input");
  r->r = vm.newBytesFrom(out.data(), out.size());
}
static const char* kB32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
E7_NATIVE(e_b32Encode) {
  E7_UNUSED;
  BytesObj* b = B(vm, a[0]);
  std::string out;
  uint64_t acc = 0;
  int bits = 0;
  for (uint32_t i = 0; i < b->len; i++) {
    acc = (acc << 8) | b->ptr()[i];
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      out += kB32[(acc >> bits) & 31];
    }
  }
  if (bits) out += kB32[(acc << (5 - bits)) & 31];
  while (out.size() % 8) out += '=';
  r->r = vm.newString(a16(out));
}
E7_NATIVE(e_b32Decode) {
  E7_UNUSED;
  std::string s = asciiOf(vm, S(vm, a[0]));
  std::vector<uint8_t> out;
  uint64_t acc = 0;
  int bits = 0;
  bool pad = false;
  for (char c : s) {
    if (c == '=') { pad = true; continue; }
    if (pad) vm.error("base32Decode: invalid input");
    const char* p = strchr(kB32, toupper((unsigned char)c));
    if (!p || !c) vm.error("base32Decode: invalid character");
    acc = (acc << 5) | (uint64_t)(p - kB32);
    bits += 5;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((uint8_t)(acc >> bits));
    }
  }
  r->r = vm.newBytesFrom(out.data(), out.size());
}
E7_NATIVE(e_utf8Encode) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  std::string u8 = utf16ToUtf8(s->chars(), s->len);
  r->r = vm.newBytesFrom(reinterpret_cast<const uint8_t*>(u8.data()), u8.size());
}
std::u16string utf8DecodeWhatwg(const uint8_t* p, size_t n);
E7_NATIVE(e_utf8Decode) {
  E7_UNUSED;
  BytesObj* b = B(vm, a[0]);
  r->r = vm.newString(utf8DecodeWhatwg(b->ptr(), b->len));
}
E7_NATIVE(e_pemDecode) {
  E7_UNUSED;
  std::string s = asciiOf(vm, S(vm, a[0]));
  std::string want = a[1].r ? strUtf8(S(vm, a[1])) : "";
  size_t p = 0;
  while (true) {
    size_t b = s.find("-----BEGIN ", p);
    if (b == std::string::npos) vm.error("pemDecode: no PEM block%s%s found", want.empty() ? "" : " labeled ", want.c_str());
    size_t le = s.find("-----", b + 11);
    if (le == std::string::npos) vm.error("pemDecode: malformed header");
    std::string label = s.substr(b + 11, le - b - 11);
    size_t bodyStart = le + 5;
    std::string endMark = "-----END " + label + "-----";
    size_t e = s.find(endMark, bodyStart);
    if (e == std::string::npos) vm.error("pemDecode: missing END line");
    if (!want.empty() && label != want) { p = e + endMark.size(); continue; }
    std::string body = s.substr(bodyStart, e - bodyStart);
    std::vector<uint8_t> out;
    if (!b64dec(body, out)) vm.error("pemDecode: invalid base64 body");
    r->r = vm.newBytesFrom(out.data(), out.size());
    return;
  }
}
E7_NATIVE(e_pemEncode) {
  E7_UNUSED;
  BytesObj* b = B(vm, a[0]);
  std::string label = strUtf8(S(vm, a[1]));
  std::string body = b64enc(b->ptr(), b->len, false);
  std::string out = "-----BEGIN " + label + "-----\n";
  for (size_t i = 0; i < body.size(); i += 64) out += body.substr(i, 64) + "\n";
  out += "-----END " + label + "-----\n";
  r->r = vm.newString(a16(out));
}
E7_NATIVE(e_concatBytes) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  if (arr->vt != VT_REF) vm.error("concatBytes: expected an array of Uint8Array");
  uint64_t total = 0;
  for (uint32_t i = 0; i < arr->len; i++) total += B(vm, arr->data[i])->len;
  if (total > 0x7FFFFFFF) vm.error("concatBytes: result too large");
  BytesObj* out = vm.newBytes((uint32_t)total);
  size_t off = 0;
  for (uint32_t i = 0; i < arr->len; i++) {
    BytesObj* b = static_cast<BytesObj*>(arr->data[i].r);
    if (b->len) memcpy(out->ptr() + off, b->ptr(), b->len);
    off += b->len;
  }
  r->r = out;
}
E7_NATIVE(e_bytesEqual) {
  E7_UNUSED;
  BytesObj* x = B(vm, a[0]);
  BytesObj* y = B(vm, a[1]);
  r->i = x->len == y->len && (x->len == 0 || memcmp(x->ptr(), y->ptr(), x->len) == 0);
}

E7_NATIVE(e_crc32) { E7_UNUSED; BytesObj* d = B(vm, a[0]); r->f = crc32(d->ptr(), d->len); }

void registerEncodingNatives(VM& vm) {
  vm.registerNative("encoding.hexEncode", "R>R", e_hexEncode);
  vm.registerNative("encoding.hexDecode", "R>R", e_hexDecode);
  vm.registerNative("encoding.base64Encode", "R>R", e_b64Encode);
  vm.registerNative("encoding.base64Decode", "R>R", e_b64Decode);
  vm.registerNative("encoding.base64UrlEncode", "R>R", e_b64UrlEncode);
  vm.registerNative("encoding.base64UrlDecode", "R>R", e_b64Decode);
  vm.registerNative("encoding.base32Encode", "R>R", e_b32Encode);
  vm.registerNative("encoding.base32Decode", "R>R", e_b32Decode);
  vm.registerNative("encoding.utf8Encode", "R>R", e_utf8Encode);
  vm.registerNative("encoding.utf8Decode", "R>R", e_utf8Decode);
  vm.registerNative("encoding.pemDecode", "RR>R", e_pemDecode);
  vm.registerNative("encoding.pemEncode", "RR>R", e_pemEncode);
  vm.registerNative("encoding.concatBytes", "R>R", e_concatBytes);
  vm.registerNative("encoding.bytesEqual", "RR>I", e_bytesEqual);
  vm.registerNative("encoding.crc32", "R>F", e_crc32);
}

}  // namespace e7
