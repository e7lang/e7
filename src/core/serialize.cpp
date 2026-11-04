// .e7b binary format (little endian). See docs/BYTECODE.md.
#include <cstring>

#include "module.h"

namespace e7 {

namespace {
struct CrcTable {
  uint32_t t[256];
  CrcTable() {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
  }
};
}  // namespace

uint32_t crc32(const uint8_t* p, size_t n) {
  static const CrcTable table;  // thread-safe initialization (C++11 magic static)
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) c = table.t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

namespace {

struct W {
  std::vector<uint8_t> b;
  void u8(uint8_t v) { b.push_back(v); }
  void u16(uint16_t v) { u8((uint8_t)v); u8((uint8_t)(v >> 8)); }
  void u32(uint32_t v) { for (int i = 0; i < 4; i++) u8((uint8_t)(v >> (8 * i))); }
  void u64(uint64_t v) { for (int i = 0; i < 8; i++) u8((uint8_t)(v >> (8 * i))); }
  void str(const std::string& s) { u32((uint32_t)s.size()); b.insert(b.end(), s.begin(), s.end()); }
};

struct Rd {
  const uint8_t* p;
  size_t n;
  size_t pos = 0;
  bool ok = true;
  bool need(size_t k) {
    if (!ok || k > n - pos) { ok = false; return false; }
    return true;
  }
  uint8_t u8() { if (!need(1)) return 0; return p[pos++]; }
  uint16_t u16() { if (!need(2)) return 0; uint16_t v = (uint16_t)(p[pos] | (p[pos + 1] << 8)); pos += 2; return v; }
  uint32_t u32() {
    if (!need(4)) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)p[pos + i] << (8 * i);
    pos += 4;
    return v;
  }
  uint64_t u64() {
    if (!need(8)) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[pos + i] << (8 * i);
    pos += 8;
    return v;
  }
  // count with sanity bound: each element needs at least `minBytes`
  uint32_t count(size_t minBytes) {
    uint32_t c = u32();
    if (!ok) return 0;
    if (minBytes && (uint64_t)c * minBytes > n - pos) { ok = false; return 0; }
    return c;
  }
  std::string str() {
    uint32_t len = count(1);
    if (!ok) return {};
    std::string s(reinterpret_cast<const char*>(p + pos), len);
    pos += len;
    return s;
  }
};

const char kMagic[4] = {'E', '7', 'B', 0};
const char kFooter[4] = {'E', '7', 'B', '$'};

}  // namespace

std::vector<uint8_t> serializeModule(const Module& m) {
  W w;
  w.b.insert(w.b.end(), kMagic, kMagic + 4);
  w.u32(kFormatVersion);
  w.u32(0);  // flags
  w.str(m.name);
  w.u32((uint32_t)m.ints.size());
  for (int64_t v : m.ints) w.u64((uint64_t)v);
  w.u32((uint32_t)m.nums.size());
  for (double d : m.nums) { uint64_t u; memcpy(&u, &d, 8); w.u64(u); }
  w.u32((uint32_t)m.strs.size());
  for (auto& s : m.strs) { w.u32((uint32_t)s.size()); for (char16_t c : s) w.u16(c); }
  w.u32((uint32_t)m.sigs.size());
  for (auto& s : m.sigs) w.str(s);
  w.u32((uint32_t)m.natives.size());
  for (auto& n : m.natives) { w.str(n.name); w.u32(n.sig); }
  w.u32((uint32_t)m.classes.size());
  for (auto& c : m.classes) {
    w.str(c.name);
    w.u32((uint32_t)c.parent);
    w.u8(c.isRecord);
    w.u32((uint32_t)c.fields.size());
    for (auto& f : c.fields) { w.str(f.name); w.u8(f.vt); }
    w.u32((uint32_t)c.vtable.size());
    for (size_t i = 0; i < c.vtable.size(); i++) { w.u32(c.vtable[i]); w.str(c.methodNames[i]); }
  }
  w.u32((uint32_t)m.funcs.size());
  for (auto& f : m.funcs) {
    w.str(f.name);
    w.u32(f.sig);
    w.u16(f.nparams);
    w.u32((uint32_t)f.regKinds.size());
    for (uint8_t k : f.regKinds) w.u8(k);
    w.u32((uint32_t)f.upvalKinds.size());
    for (uint8_t k : f.upvalKinds) w.u8(k);
    w.u32((uint32_t)f.icNames.size());
    for (uint32_t s : f.icNames) w.u32(s);
    w.u32((uint32_t)f.code.size());
    for (uint64_t c : f.code) w.u64(c);
    w.u32((uint32_t)f.lines.size());
    for (uint32_t l : f.lines) w.u32(l);
  }
  w.u32((uint32_t)m.globals.size());
  for (auto& g : m.globals) { w.str(g.name); w.u8(g.vt); }
  w.u32((uint32_t)m.exports.size());
  for (auto& e : m.exports) { w.str(e.name); w.u32(e.func); w.str(e.hostSig); }
  w.u32(m.initFunc);
  uint32_t crc = crc32(w.b.data(), w.b.size());
  w.b.insert(w.b.end(), kFooter, kFooter + 4);
  w.u32(crc);
  return std::move(w.b);
}

bool deserializeModule(const uint8_t* data, size_t len, Module& m, std::string& err) {
  m = Module();
  if (len < 20 || memcmp(data, kMagic, 4) != 0) { err = "not an e7b file"; return false; }
  // footer
  if (memcmp(data + len - 8, kFooter, 4) != 0) { err = "missing e7b footer"; return false; }
  uint32_t want = (uint32_t)data[len - 4] | ((uint32_t)data[len - 3] << 8) | ((uint32_t)data[len - 2] << 16) |
                  ((uint32_t)data[len - 1] << 24);
  if (crc32(data, len - 8) != want) { err = "checksum mismatch (corrupted file)"; return false; }
  Rd r{data, len - 8};
  r.pos = 4;
  uint32_t ver = r.u32();
  if (ver != kFormatVersion) { err = "unsupported e7b format version " + std::to_string(ver); return false; }
  r.u32();  // flags
  m.name = r.str();
  uint32_t n = r.count(8);
  m.ints.resize(n);
  for (auto& v : m.ints) v = (int64_t)r.u64();
  n = r.count(8);
  m.nums.resize(n);
  for (auto& d : m.nums) { uint64_t u = r.u64(); memcpy(&d, &u, 8); }
  n = r.count(4);
  m.strs.resize(n);
  for (auto& s : m.strs) {
    uint32_t l = r.count(2);
    s.resize(l);
    for (auto& c : s) c = r.u16();
  }
  n = r.count(4);
  m.sigs.resize(n);
  for (auto& s : m.sigs) s = r.str();
  n = r.count(8);
  m.natives.resize(n);
  for (auto& x : m.natives) { x.name = r.str(); x.sig = r.u32(); }
  n = r.count(17);
  m.classes.resize(n);
  for (auto& c : m.classes) {
    c.name = r.str();
    c.parent = (int32_t)r.u32();
    c.isRecord = r.u8();
    uint32_t nf = r.count(5);
    c.fields.resize(nf);
    for (auto& f : c.fields) { f.name = r.str(); f.vt = r.u8(); }
    uint32_t nv = r.count(8);
    c.vtable.resize(nv);
    c.methodNames.resize(nv);
    for (uint32_t i = 0; i < nv; i++) { c.vtable[i] = r.u32(); c.methodNames[i] = r.str(); }
    if (!r.ok) break;
  }
  n = r.count(30);
  m.funcs.resize(n);
  for (auto& f : m.funcs) {
    f.name = r.str();
    f.sig = r.u32();
    f.nparams = r.u16();
    uint32_t k = r.count(1);
    f.regKinds.resize(k);
    for (auto& x : f.regKinds) x = r.u8();
    k = r.count(1);
    f.upvalKinds.resize(k);
    for (auto& x : f.upvalKinds) x = r.u8();
    k = r.count(4);
    f.icNames.resize(k);
    for (auto& x : f.icNames) x = r.u32();
    k = r.count(8);
    f.code.resize(k);
    for (auto& x : f.code) x = r.u64();
    k = r.count(4);
    f.lines.resize(k);
    for (auto& x : f.lines) x = r.u32();
    if (!r.ok) break;
  }
  n = r.count(5);
  m.globals.resize(n);
  for (auto& g : m.globals) { g.name = r.str(); g.vt = r.u8(); }
  n = r.count(12);
  m.exports.resize(n);
  for (auto& e : m.exports) { e.name = r.str(); e.func = r.u32(); e.hostSig = r.str(); }
  m.initFunc = r.u32();
  if (!r.ok) { err = "truncated or malformed e7b file"; return false; }
  if (r.pos != r.n) { err = "trailing data in e7b file"; return false; }
  return true;
}

}  // namespace e7
