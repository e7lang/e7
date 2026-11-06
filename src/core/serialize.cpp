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

// ---- debug-information extension of the line table (see kLineExtDebug in module.h)
void putName(std::vector<uint32_t>& w, const std::string& s) {
  w.push_back((uint32_t)s.size());
  for (size_t i = 0; i < s.size(); i += 4) {
    uint32_t x = 0;
    for (size_t k = 0; k < 4 && i + k < s.size(); k++) x |= (uint32_t)(uint8_t)s[i + k] << (8 * k);
    w.push_back(x);
  }
}

std::vector<uint32_t> linesWithDebug(const FuncDesc& f) {
  std::vector<uint32_t> L = f.lines;
  if (!f.hasDebug) return L;
  const FuncDebug& d = f.debug;
  std::vector<uint32_t> p;
  p.push_back((uint32_t)d.pos.size());
  for (auto& e : d.pos) { p.push_back(e.pc); p.push_back(e.line); p.push_back(e.col); }
  p.push_back((uint32_t)d.stmts.size());
  for (auto& e : d.stmts) { p.push_back(e.pc); p.push_back(e.line); p.push_back(e.col); }
  p.push_back((uint32_t)d.locals.size());
  for (auto& l : d.locals) {
    p.push_back(l.reg);
    p.push_back(l.startPc);
    p.push_back(l.endPc);
    p.push_back((uint32_t)l.vt | ((uint32_t)l.flags << 8));
    putName(p, l.name);
  }
  p.push_back((uint32_t)d.upvalNames.size());
  for (auto& n : d.upvalNames) putName(p, n);
  L.push_back(kLineExtMarker);
  L.push_back(kLineExtDebug);
  L.push_back((uint32_t)p.size());
  L.insert(L.end(), p.begin(), p.end());
  if (L.size() % 2) L.push_back(0);  // the table holds pairs
  return L;
}

// Finds and decodes the debug extension of f.lines (removing it from the table). Returns false on a
// malformed extension. Every count is checked against the words that remain.
bool extractDebug(FuncDesc& f) {
  std::vector<uint32_t>& L = f.lines;
  size_t n = L.size();
  size_t i = 0;
  while (i + 1 < n && L[i] != kLineExtMarker) i += 2;
  if (i + 1 < n && L[i + 1] == kLineExtFiles) {
    size_t j = i + 2;
    while (j + 3 < n && L[j] == kLineExtMarker && L[j + 2] == kLineExtMarker) j += 4;
    i = j;
  }
  if (!(i + 2 < n && L[i] == kLineExtMarker && L[i + 1] == kLineExtDebug)) return true;
  uint64_t words = L[i + 2];
  size_t at = i + 3;
  if (words > n - at) return false;
  size_t end = at + (size_t)words;
  if (n - end > 1 || (n - end == 1 && L[end] != 0)) return false;  // only one padding word may follow
  auto take = [&](uint32_t& out) {
    if (at >= end) return false;
    out = L[at++];
    return true;
  };
  auto count = [&](uint32_t& out, size_t per) {
    if (!take(out)) return false;
    return (uint64_t)out * per <= (uint64_t)(end - at);
  };
  auto name = [&](std::string& s) {
    uint32_t len;
    if (!take(len) || len > 4096 || (uint64_t)(len + 3) / 4 > (uint64_t)(end - at)) return false;
    s.resize(len);
    for (uint32_t k = 0; k < len; k++) s[k] = (char)(uint8_t)(L[at + k / 4] >> (8 * (k % 4)));
    at += (len + 3) / 4;
    return true;
  };
  FuncDebug d;
  uint32_t c;
  if (!count(c, 3)) return false;
  d.pos.resize(c);
  for (auto& e : d.pos) { take(e.pc); take(e.line); take(e.col); }
  if (!count(c, 3)) return false;
  d.stmts.resize(c);
  for (auto& e : d.stmts) { take(e.pc); take(e.line); take(e.col); }
  if (!count(c, 5)) return false;
  d.locals.resize(c);
  for (auto& l : d.locals) {
    uint32_t tf = 0;
    if (!take(l.reg) || !take(l.startPc) || !take(l.endPc) || !take(tf) || (tf >> 16) != 0) return false;
    l.vt = (uint8_t)tf;
    l.flags = (uint8_t)(tf >> 8);
    if (!name(l.name)) return false;
  }
  if (!count(c, 1)) return false;
  d.upvalNames.resize(c);
  for (auto& s : d.upvalNames)
    if (!name(s)) return false;
  if (at != end) return false;
  L.resize(i);
  f.debug = std::move(d);
  f.hasDebug = true;
  return true;
}

const char kMagic[4] = {'E', '7', 'B', 0};
const char kFooter[4] = {'E', '7', 'B', '$'};

}  // namespace

std::vector<uint8_t> serializeModule(const Module& m) {
  W w;
  w.b.insert(w.b.end(), kMagic, kMagic + 4);
  w.u32(kFormatVersion);
  bool anyDebug = false;
  for (auto& f : m.funcs) anyDebug = anyDebug || f.hasDebug;
  w.u32(anyDebug ? (m.flags | kModuleFlagDebug) : (m.flags & ~kModuleFlagDebug));  // flags
  w.u32(m.minVm);
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
  w.u32((uint32_t)m.regexps.size());
  for (auto& r : m.regexps) { w.str(r.source); w.str(r.flags); }
  w.u32((uint32_t)m.classes.size());
  for (auto& c : m.classes) {
    w.str(c.name);
    w.u32((uint32_t)c.parent);
    w.u8(c.isRecord);
    w.u8(c.cflags);
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
    w.u8(f.fflags);
    w.u32((uint32_t)f.regKinds.size());
    for (uint8_t k : f.regKinds) w.u8(k);
    w.u32((uint32_t)f.upvalKinds.size());
    for (uint8_t k : f.upvalKinds) w.u8(k);
    w.u32((uint32_t)f.icNames.size());
    for (uint32_t s : f.icNames) w.u32(s);
    w.u32((uint32_t)f.code.size());
    for (uint64_t c : f.code) w.u64(c);
    w.u32((uint32_t)f.handlers.size());
    for (auto& h : f.handlers) { w.u32(h.start); w.u32(h.end); w.u32(h.handler); w.u32(h.reg); }
    std::vector<uint32_t> L = linesWithDebug(f);
    w.u32((uint32_t)L.size());
    for (uint32_t l : L) w.u32(l);
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

bool readModuleHeader(const uint8_t* data, size_t len, ModuleHeader& out) {
  out = ModuleHeader();
  if (!data || len < 24 || memcmp(data, kMagic, 4) != 0) return false;
  auto u32at = [&](size_t i) {
    return (uint32_t)data[i] | ((uint32_t)data[i + 1] << 8) | ((uint32_t)data[i + 2] << 16) | ((uint32_t)data[i + 3] << 24);
  };
  out.format = u32at(4);
  if (out.format == kFormatVersion) {
    out.flags = u32at(8);
    out.minVm = u32at(12);
  }
  return true;
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
  if (ver != kFormatVersion) {
    err = "unsupported e7b format " + std::to_string(ver) + " (this e7vm reads format " + std::to_string(kFormatVersion) + ")";
    return false;
  }
  m.flags = r.u32();  // informational (unknown bits are ignored)
  m.minVm = r.u32();
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
  n = r.count(8);
  m.regexps.resize(n);
  for (auto& x : m.regexps) { x.source = r.str(); x.flags = r.str(); }
  n = r.count(18);
  m.classes.resize(n);
  for (auto& c : m.classes) {
    c.name = r.str();
    c.parent = (int32_t)r.u32();
    c.isRecord = r.u8();
    c.cflags = r.u8();
    uint32_t nf = r.count(5);
    c.fields.resize(nf);
    for (auto& f : c.fields) { f.name = r.str(); f.vt = r.u8(); }
    uint32_t nv = r.count(8);
    c.vtable.resize(nv);
    c.methodNames.resize(nv);
    for (uint32_t i = 0; i < nv; i++) { c.vtable[i] = r.u32(); c.methodNames[i] = r.str(); }
    if (!r.ok) break;
  }
  n = r.count(31);
  m.funcs.resize(n);
  for (auto& f : m.funcs) {
    f.name = r.str();
    f.sig = r.u32();
    f.nparams = r.u16();
    f.fflags = r.u8();
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
    k = r.count(16);
    f.handlers.resize(k);
    for (auto& h : f.handlers) { h.start = r.u32(); h.end = r.u32(); h.handler = r.u32(); h.reg = r.u32(); }
    k = r.count(4);
    f.lines.resize(k);
    for (auto& x : f.lines) x = r.u32();
    if (!r.ok) break;
    if (!extractDebug(f)) { err = "malformed debug information in e7b file"; return false; }
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
