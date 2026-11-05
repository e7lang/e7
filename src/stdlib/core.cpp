// Core natives: value conversion / formatting, console, globals, Number, Text, Object, Date.
#include <chrono>
#include <cstdio>
#include <unordered_set>

#include "util.h"

namespace e7 {

ModuleRT* currentModule(VM& vm) { return vm.currentModule(); }
std::string dateToISO(double t);
std::string dateToString(double t);

std::u16string intToU16(int64_t v, uint8_t vt) {
  std::string s;
  if (vt == VT_BOOL) s = v ? "true" : "false";
  else if (vt == VT_U64) s = uint64ToString((uint64_t)v);
  else s = int64ToString(v);
  return std::u16string(s.begin(), s.end());
}

static std::u16string ascii(const std::string& s) { return std::u16string(s.begin(), s.end()); }

Slot boxValue(VM& vm, Slot v, uint8_t vt) {
  if (vt == VT_REF) return v;
  return slotR(vm.newBox(vt, v));
}

// arrays currently being converted (JS Array.prototype.join skips cyclic references)
static thread_local std::vector<const Obj*> tlJoinStack;
struct JoinGuard {
  explicit JoinGuard(VM& vm, const Obj* o) {
    if (tlJoinStack.size() >= 256) vm.error("array nesting is too deep to convert to a string (an e7 limit of 256 levels)");
    tlJoinStack.push_back(o);
  }
  ~JoinGuard() { tlJoinStack.pop_back(); }
};
static bool joining(const Obj* o) {
  for (const Obj* x : tlJoinStack)
    if (x == o) return true;
  return false;
}

std::u16string valueToString(VM& vm, Slot v, uint8_t vt) {
  if (vt == VT_NUMBER) return ascii(numberToString(v.f));
  if (vt != VT_REF) return intToU16(v.i, vt);
  Obj* o = v.r;
  if (!o) return u"null";
  if (o == vm.undef()) return u"undefined";
  switch (o->type) {
    case OT::String: return strU16(static_cast<StrObj*>(o));
    case OT::Box: {
      auto* b = static_cast<BoxObj*>(o);
      if (b->flags & 1) { std::string ds = dateToString(b->v.f); return std::u16string(ds.begin(), ds.end()); }
      return valueToString(vm, b->v, b->vt);
    }
    case OT::BigInt: return ascii(bigToString(static_cast<BigObj*>(o), 10));
    case OT::Array: {
      auto* a = static_cast<ArrObj*>(o);
      StrObj *en, *em;
      if (vm.errorParts(o, &en, &em)) return em->len ? strU16(en) + u": " + strU16(em) : strU16(en);
      if (joining(o)) return u"";
      JoinGuard g(vm, o);
      std::u16string out;
      for (uint32_t i = 0; i < a->len; i++) {
        if (i) out += u',';
        Slot e = a->data[i];
        if (a->vt == VT_REF && (!e.r || e.r == vm.undef())) continue;
        out += valueToString(vm, e, a->vt);
        vm.checkTemp(out.size() * 2);
      }
      return out;
    }
    case OT::Bytes: {
      auto* b = static_cast<BytesObj*>(o);
      std::u16string out;
      for (uint32_t i = 0; i < b->len; i++) {
        if (i) out += u',';
        out += ascii(std::to_string(b->ptr()[i]));
      }
      return out;
    }
    case OT::Map: return static_cast<MapObj*>(o)->isSet ? u"[object Set]" : u"[object Map]";
    case OT::Instance: {
      auto* in = static_cast<InstObj*>(o);
      if (!in->cls->isTuple) return u"[object Object]";
      if (joining(o)) return u"";
      JoinGuard g(vm, o);
      std::u16string out;
      for (uint32_t i = 0; i < in->cls->nfields; i++) {
        if (i) out += u',';
        Slot e = in->fields()[i];
        if (in->cls->fieldVT[i] == VT_REF && (!e.r || e.r == vm.undef())) continue;
        out += valueToString(vm, e, in->cls->fieldVT[i]);
        vm.checkTemp(out.size() * 2);
      }
      return out;
    }
    case OT::Closure: return u"function " + utf8ToUtf16(static_cast<CloObj*>(o)->fn->name) + u"() { [native code] }";
    case OT::Buffer: return u"[object ArrayBuffer]";
    case OT::Handle: return u"[object KeyHandle]";
    default: return u"[object Object]";
  }
}

bool strictEquals(VM& vm, Slot a, Slot b, uint8_t vt) {
  if (vt == VT_NUMBER) return a.f == b.f;
  if (vt != VT_REF) return a.i == b.i;
  Obj* x = a.r;
  Obj* y = b.r;
  if (x && y && x->type == OT::Box && y->type == OT::Box && !((x->flags | y->flags) & 1)) {
    auto* bx = static_cast<BoxObj*>(x);
    auto* by = static_cast<BoxObj*>(y);
    if (kindOfVT(bx->vt) != kindOfVT(by->vt)) return false;
    return strictEquals(vm, bx->v, by->v, bx->vt);
  }
  if (x == y) return true;
  if (!x || !y) return false;
  if (x->type == OT::String && y->type == OT::String) return strEq(static_cast<StrObj*>(x), static_cast<StrObj*>(y));
  if (x->type == OT::BigInt && y->type == OT::BigInt) return bigCmp(static_cast<BigObj*>(x), static_cast<BigObj*>(y)) == 0;
  return false;
}

bool sameValueZero(VM& vm, Slot a, Slot b, uint8_t vt) {
  if (vt == VT_NUMBER) return a.f == b.f || (a.f != a.f && b.f != b.f);
  return strictEquals(vm, a, b, vt);
}

static bool isIdent(const std::string& s) {
  if (s.empty()) return false;
  if (!(isalpha((unsigned char)s[0]) || s[0] == '_' || s[0] == '$')) return false;
  for (char c : s)
    if (!(isalnum((unsigned char)c) || c == '_' || c == '$')) return false;
  return true;
}

static std::string quoteSingle(const std::u16string& s) {
  std::string body = utf16ToUtf8(s);
  bool hasSingle = body.find('\'') != std::string::npos;
  bool hasDouble = body.find('"') != std::string::npos;
  char q = (hasSingle && !hasDouble) ? '"' : '\'';
  std::string out(1, q);
  for (char c : body) {
    if (c == q) { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += "\\t";
    else if (c == '\r') out += "\\r";
    else if (c == '\\') out += "\\\\";
    else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\x%02X", (unsigned char)c); out += b; }
    else out += c;
  }
  out += q;
  return out;
}

static std::string inspectRec(VM& vm, Slot v, uint8_t vt, int depth, bool top, std::unordered_set<Obj*>& seen) {
  if (vt == VT_NUMBER) {
    if (v.f == 0 && std::signbit(v.f)) return "-0";
    return numberToString(v.f);
  }
  if (vt != VT_REF) return utf16ToUtf8(intToU16(v.i, vt));
  Obj* o = v.r;
  if (!o) return "null";
  if (o == vm.undef()) return "undefined";
  switch (o->type) {
    case OT::String: {
      auto* s = static_cast<StrObj*>(o);
      return top ? strUtf8(s) : quoteSingle(strU16(s));
    }
    case OT::Box: {
      auto* b = static_cast<BoxObj*>(o);
      if (b->flags & 1) return b->v.f != b->v.f ? "Invalid Date" : dateToISO(b->v.f);
      return inspectRec(vm, b->v, b->vt, depth, top, seen);
    }
    case OT::BigInt: return bigToString(static_cast<BigObj*>(o), 10) + "n";
    case OT::Closure: {
      std::string nm = static_cast<CloObj*>(o)->fn->name;
      if (nm.empty() || nm[0] == '<') return "[Function (anonymous)]";
      size_t dot = nm.find_last_of('.');
      if (dot != std::string::npos) nm = nm.substr(dot + 1);
      return "[Function: " + nm + "]";
    }
    case OT::Handle: return "KeyHandle {}";
    case OT::Buffer: return "ArrayBuffer { byteLength: " + std::to_string(static_cast<BufObj*>(o)->len) + " }";
    default: break;
  }
  if (seen.count(o)) return "[Circular *1]";
  StrObj *en, *em;
  if (vm.errorParts(o, &en, &em))  // the compiler rejects printing Errors (Node prints a stack trace)
    return em->len ? strUtf8(en) + ": " + strUtf8(em) : strUtf8(en);
  if (o->type == OT::Array) {
    auto* a = static_cast<ArrObj*>(o);
    if (a->len == 0) return "[]";
    if (depth > 2) return "[Array]";
    seen.insert(o);
    std::string out = "[ ";
    uint32_t shown = a->len > 100 ? 100 : a->len;
    for (uint32_t i = 0; i < shown; i++) {
      if (i) out += ", ";
      out += inspectRec(vm, a->data[i], a->vt, depth + 1, false, seen);
    }
    if (a->len > shown) out += ", ... " + std::to_string(a->len - shown) + " more items";
    out += " ]";
    seen.erase(o);
    return out;
  }
  if (o->type == OT::Bytes) {
    auto* b = static_cast<BytesObj*>(o);
    std::string out = "Uint8Array(" + std::to_string(b->len) + ") [";
    if (b->len == 0) return out + "]";
    out += " ";
    uint32_t shown = b->len > 100 ? 100 : b->len;
    for (uint32_t i = 0; i < shown; i++) {
      if (i) out += ", ";
      out += std::to_string(b->ptr()[i]);
    }
    if (b->len > shown) out += ", ... " + std::to_string(b->len - shown) + " more items";
    return out + " ]";
  }
  if (o->type == OT::Map) {
    auto* m = static_cast<MapObj*>(o);
    std::string out = std::string(m->isSet ? "Set(" : "Map(") + std::to_string(m->count) + ") {";
    if (m->count == 0) return out + "}";
    if (depth > 2) return m->isSet ? "[Set]" : "[Map]";
    seen.insert(o);
    out += " ";
    bool first = true;
    for (auto& e : m->entries) {
      if (!e.live) continue;
      if (!first) out += ", ";
      first = false;
      out += inspectRec(vm, e.k, m->kvt, depth + 1, false, seen);
      if (!m->isSet) out += " => " + inspectRec(vm, e.v, m->vvt, depth + 1, false, seen);
    }
    seen.erase(o);
    return out + " }";
  }
  if (o->type == OT::Instance && static_cast<InstObj*>(o)->cls->isTuple) {
    auto* in = static_cast<InstObj*>(o);
    if (in->cls->nfields == 0) return "[]";
    if (depth > 2) return "[Array]";
    seen.insert(o);
    std::string out = "[ ";
    for (uint32_t i = 0; i < in->cls->nfields; i++) {
      if (i) out += ", ";
      out += inspectRec(vm, in->fields()[i], in->cls->fieldVT[i], depth + 1, false, seen);
    }
    seen.erase(o);
    return out + " ]";
  }
  if (o->type == OT::Instance) {
    auto* in = static_cast<InstObj*>(o);
    ClassRT* c = in->cls;
    std::string prefix = c->isRecord ? "" : c->name + " ";
    if (c->nfields == 0) return prefix + "{}";
    if (depth > 2) return c->isRecord ? "[Object]" : "[" + c->name + "]";
    seen.insert(o);
    std::string out = prefix + "{ ";
    bool first = true;
    for (uint32_t i = 0; i < c->nfields; i++) {
      Slot fv = in->fields()[i];
      if (!first) out += ", ";
      first = false;
      const std::string& k = c->fieldNames[i];
      out += (isIdent(k) ? k : quoteSingle(utf8ToUtf16(k))) + ": " + inspectRec(vm, fv, c->fieldVT[i], depth + 1, false, seen);
    }
    seen.erase(o);
    return out + " }";
  }
  return "[object]";
}

std::string inspectValue(VM& vm, Slot v, uint8_t vt, int depth, bool top) {
  std::unordered_set<Obj*> seen;
  return inspectRec(vm, v, vt, depth, top, seen);
}

// ------------------------------------------------------------------ natives
E7_NATIVE(n_valueToString) { E7_UNUSED; r->r = vm.newString(valueToString(vm, a[0], VT_REF)); }
E7_NATIVE(n_valueTruthy) {
  E7_UNUSED;
  Obj* o = a[0].r;
  bool t;
  if (!o || o == vm.undef()) t = false;
  else if (o->type == OT::String) t = static_cast<StrObj*>(o)->len > 0;
  else if (o->type == OT::Box) {
    auto* b = static_cast<BoxObj*>(o);
    if (b->vt == VT_NUMBER) t = !(b->v.f == 0 || b->v.f != b->v.f);
    else t = b->v.i != 0;
  } else if (o->type == OT::BigInt) t = !static_cast<BigObj*>(o)->mag.empty();
  else t = true;
  r->i = t;
}
E7_NATIVE(n_strictEq) { E7_UNUSED; r->i = strictEquals(vm, a[0], a[1], VT_REF); }

static void logImpl(VM& vm, Slot arr, bool err) {
  ArrObj* a = A(vm, arr);
  std::string line;
  for (uint32_t i = 0; i < a->len; i++) {
    if (i) line += ' ';
    line += inspectValue(vm, a->data[i], a->vt, 0, true);
  }
  if (err && !vm.printFn) {
    fwrite(line.data(), 1, line.size(), stderr);
    fputc('\n', stderr);
    return;
  }
  vm.print(line);
}
E7_NATIVE(n_consoleLog) { E7_UNUSED; logImpl(vm, a[0], false); }
E7_NATIVE(n_consoleError) { E7_UNUSED; logImpl(vm, a[0], true); }

E7_NATIVE(n_number) { E7_UNUSED; r->f = stringToNumber(strU16(S(vm, a[0]))); }
E7_NATIVE(n_parseFloat) { E7_UNUSED; r->f = parseFloatJS(strU16(S(vm, a[0]))); }
E7_NATIVE(n_parseInt) {
  E7_UNUSED;
  double rd = toInteger(a[1].f);
  int radix = (rd != rd || rd == 0) ? 0 : (int)(int32_t)doubleToInt64Bits(rd);
  if (radix != 0 && (radix < 2 || radix > 36)) { r->f = NAN; return; }
  r->f = parseIntJS(strU16(S(vm, a[0])), radix);
}

static const char* kHex = "0123456789ABCDEF";
static std::u16string uriEncode(VM& vm, const StrObj* s, const char* unreserved) {
  std::u16string out;
  const char16_t* c = s->chars();
  for (uint32_t i = 0; i < s->len; i++) {
    char16_t ch = c[i];
    if (ch > 0 && ch < 128 && (isalnum(ch) || strchr(unreserved, (char)ch))) { out += ch; continue; }  // strchr finds the NUL
    uint32_t cp = ch;
    if (ch >= 0xDC00 && ch <= 0xDFFF) vm.error("URIError: URI malformed");
    if (ch >= 0xD800 && ch <= 0xDBFF) {
      if (i + 1 >= s->len || c[i + 1] < 0xDC00 || c[i + 1] > 0xDFFF) vm.error("URIError: URI malformed");
      cp = 0x10000 + ((ch - 0xD800) << 10) + (c[i + 1] - 0xDC00);
      i++;
    }
    char16_t tmp[2];
    size_t tn = 0;
    if (cp >= 0x10000) { tmp[0] = (char16_t)(0xD800 + ((cp - 0x10000) >> 10)); tmp[1] = (char16_t)(0xDC00 + ((cp - 0x10000) & 0x3FF)); tn = 2; }
    else { tmp[0] = (char16_t)cp; tn = 1; }
    std::string u8 = utf16ToUtf8(tmp, tn);
    for (unsigned char b : u8) {
      out += u'%';
      out += (char16_t)kHex[b >> 4];
      out += (char16_t)kHex[b & 15];
    }
  }
  return out;
}
static std::u16string uriDecode(VM& vm, const StrObj* s, const char* reserved) {
  std::u16string out;
  const char16_t* c = s->chars();
  auto hv = [](char16_t h) -> int {
    if (h >= '0' && h <= '9') return h - '0';
    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
    return -1;
  };
  for (uint32_t i = 0; i < s->len; i++) {
    if (c[i] != '%') { out += c[i]; continue; }
    if (i + 2 >= s->len || hv(c[i + 1]) < 0 || hv(c[i + 2]) < 0) vm.error("URIError: URI malformed");
    int b = hv(c[i + 1]) * 16 + hv(c[i + 2]);
    if (b < 0x80) {
      if (strchr(reserved, b) && b) { out += c[i]; out += c[i + 1]; out += c[i + 2]; }
      else out += (char16_t)b;
      i += 2;
      continue;
    }
    int n = (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
    if (!n) vm.error("URIError: URI malformed");
    std::string bytes(1, (char)b);
    uint32_t j = i + 3;
    for (int k = 1; k < n; k++) {
      if (j + 2 >= s->len + 0 || c[j] != '%' || hv(c[j + 1]) < 0 || hv(c[j + 2]) < 0) vm.error("URIError: URI malformed");
      int bb = hv(c[j + 1]) * 16 + hv(c[j + 2]);
      if ((bb & 0xC0) != 0x80) vm.error("URIError: URI malformed");
      bytes.push_back((char)bb);
      j += 3;
    }
    std::u16string dec = utf8ToUtf16(bytes);
    if (dec.find(u'�') != std::u16string::npos) vm.error("URIError: URI malformed");
    out += dec;
    i = j - 1;
  }
  return out;
}
E7_NATIVE(n_encodeURIComponent) { E7_UNUSED; r->r = vm.newString(uriEncode(vm, S(vm, a[0]), "-_.!~*'()")); }
E7_NATIVE(n_encodeURI) { E7_UNUSED; r->r = vm.newString(uriEncode(vm, S(vm, a[0]), "-_.!~*'();/?:@&=+$,#")); }
E7_NATIVE(n_decodeURIComponent) { E7_UNUSED; r->r = vm.newString(uriDecode(vm, S(vm, a[0]), "")); }
E7_NATIVE(n_decodeURI) { E7_UNUSED; r->r = vm.newString(uriDecode(vm, S(vm, a[0]), ";/?:@&=+$,#")); }

E7_NATIVE(n_isInteger) { E7_UNUSED; double d = a[0].f; r->i = std::isfinite(d) && std::trunc(d) == d; }
E7_NATIVE(n_isSafeInteger) {
  E7_UNUSED;
  double d = a[0].f;
  r->i = std::isfinite(d) && std::trunc(d) == d && std::fabs(d) <= 9007199254740991.0;
}
E7_NATIVE(n_isFinite) { E7_UNUSED; r->i = std::isfinite(a[0].f); }
E7_NATIVE(n_isNaN) { E7_UNUSED; r->i = a[0].f != a[0].f; }

E7_NATIVE(n_toStringRadix) {
  E7_UNUSED;
  double rd = toInteger(a[1].f);
  if (rd < 2 || rd > 36) vm.error("RangeError: toString() radix must be between 2 and 36");
  r->r = vm.newStringAscii(numberToStringRadix(a[0].f, (int)rd).c_str());
}
E7_NATIVE(n_toFixed) {
  E7_UNUSED;
  double f = toInteger(a[1].f);
  if (f < 0 || f > 100) vm.error("RangeError: toFixed() digits argument must be between 0 and 100");
  r->r = vm.newStringAscii(numberToFixed(a[0].f, (int)f).c_str());
}
E7_NATIVE(n_toPrecision) {
  E7_UNUSED;
  double x = a[0].f;
  double p = toInteger(a[1].f);
  if (!std::isfinite(x)) { r->r = vm.newStringAscii(numberToString(x).c_str()); return; }
  if (p < 1 || p > 100) vm.error("RangeError: toPrecision() argument must be between 1 and 100");
  r->r = vm.newStringAscii(numberToPrecision(x, (int)p).c_str());
}
E7_NATIVE(n_toExponential) {
  E7_UNUSED;
  double x = a[0].f;
  double fd = a[1].f;
  int f = -1;
  if (fd == fd) {
    double t = toInteger(fd);
    if (std::isfinite(x) && (t < 0 || t > 100)) vm.error("RangeError: toExponential() argument must be between 0 and 100");
    f = t >= 0 && t <= 100 ? (int)t : 0;  // non-finite x ignores the digits argument
  }
  r->r = vm.newStringAscii(numberToExponential(x, f).c_str());
}
E7_NATIVE(n_intToStringRadix) {
  E7_UNUSED;
  double rd = toInteger(a[1].f);
  if (rd < 2 || rd > 36) vm.error("RangeError: toString() radix must be between 2 and 36");
  uint8_t vt = (uint8_t)immIndex(a[2].f, VT__COUNT);
  std::string s = vt == VT_U64 ? uint64ToString(a[0].u, (int)rd) : int64ToString(a[0].i, (int)rd);
  r->r = vm.newStringAscii(s.c_str());
}

E7_NATIVE(n_dateNow) {
  E7_UNUSED;
  using namespace std::chrono;
  r->f = (double)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

E7_NATIVE(n_objectKeys) {
  E7_UNUSED;
  Obj* o = a[0].r;
  if (!o || o->type != OT::Instance) vm.error("Object.keys requires an object");
  auto* in = static_cast<InstObj*>(o);
  ArrObj* arr = vm.newArray(VT_REF, in->cls->nfields);
  for (uint32_t i = 0; i < in->cls->nfields; i++) {
    if (in->cls->fieldVT[i] == VT_REF && in->fields()[i].r == vm.undef()) continue;
    vm.arrayPush(arr, slotR(vm.newStringUtf8(in->cls->fieldNames[i])));
  }
  r->r = arr;
}

E7_NATIVE(n_textEncode) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  std::string u8 = utf16ToUtf8(s->chars(), s->len);
  r->r = vm.newBytesFrom(reinterpret_cast<const uint8_t*>(u8.data()), u8.size());
}

// WHATWG UTF-8 decode (replacement for maximal subparts)
std::u16string utf8DecodeWhatwg(const uint8_t* p, size_t n) {
  std::u16string out;
  out.reserve(n);
  size_t i = 0;
  if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) i = 3;  // BOM
  while (i < n) {
    uint8_t b = p[i];
    if (b < 0x80) { out.push_back(b); i++; continue; }
    uint32_t cp;
    int need;
    uint8_t lo = 0x80, hi = 0xBF;
    if (b >= 0xC2 && b <= 0xDF) { need = 1; cp = b & 0x1F; }
    else if (b >= 0xE0 && b <= 0xEF) { need = 2; cp = b & 0x0F; if (b == 0xE0) lo = 0xA0; if (b == 0xED) hi = 0x9F; }
    else if (b >= 0xF0 && b <= 0xF4) { need = 3; cp = b & 0x07; if (b == 0xF0) lo = 0x90; if (b == 0xF4) hi = 0x8F; }
    else { out.push_back(0xFFFD); i++; continue; }
    size_t j = i + 1;
    bool ok = true;
    for (int k = 0; k < need; k++, j++) {
      if (j >= n) { ok = false; break; }
      uint8_t c = p[j];
      if (c < lo || c > hi) { ok = false; break; }
      lo = 0x80; hi = 0xBF;
      cp = (cp << 6) | (c & 0x3F);
    }
    if (!ok) { out.push_back(0xFFFD); i = j; continue; }
    i = j;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back((char16_t)(0xD800 + (cp >> 10)));
      out.push_back((char16_t)(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back((char16_t)cp);
    }
  }
  return out;
}
E7_NATIVE(n_textDecode) {
  E7_UNUSED;
  BytesObj* b = B(vm, a[0]);
  r->r = vm.newString(utf8DecodeWhatwg(b->ptr(), b->len));
}

// Error objects (see VM::newErrorObject): [name, message]
static void errParts(VM& vm, Slot s, StrObj** name, StrObj** msg) {
  if (!vm.errorParts(s.r, name, msg)) vm.error("expected an Error object");
}
E7_NATIVE(n_errorNew) {
  E7_UNUSED;
  StrObj* name = S(vm, a[0]);
  StrObj* msg = S(vm, a[1]);
  ArrObj* e = vm.newArray(VT_REF, 2);
  e->data[0].r = name;
  e->data[1].r = msg;
  e->len = 2;
  e->flags |= kArrFlagError;
  r->r = e;
}
E7_NATIVE(n_errorName) { E7_UNUSED; StrObj *nm, *m; errParts(vm, a[0], &nm, &m); r->r = nm; }
E7_NATIVE(n_errorMessage) { E7_UNUSED; StrObj *nm, *m; errParts(vm, a[0], &nm, &m); r->r = m; }
E7_NATIVE(n_errorToString) {  // String(e): "Name: message", or "Name" when the message is empty
  E7_UNUSED;
  StrObj *nm, *m;
  errParts(vm, a[0], &nm, &m);
  if (m->len == 0) { r->r = nm; return; }
  std::u16string s = strU16(nm) + u": " + strU16(m);
  r->r = vm.newString(s);
}
E7_NATIVE(n_errorIs) {  // x instanceof Error / TypeError / ...: false for anything that is not an Error
  E7_UNUSED;
  StrObj *nm, *m;
  std::u16string want = strU16(S(vm, a[1]));
  if (!vm.errorParts(a[0].r, &nm, &m)) r->i = 0;
  else r->i = (want == u"Error" || strU16(nm) == want) ? 1 : 0;
}

void registerCoreNatives(VM& vm) {
  vm.registerNative("Error.new", "RR>R", n_errorNew);
  vm.registerNative("Error.name", "R>R", n_errorName);
  vm.registerNative("Error.message", "R>R", n_errorMessage);
  vm.registerNative("Error.toString", "R>R", n_errorToString);
  vm.registerNative("Error.is", "RR>I", n_errorIs);
  vm.registerNative("Value.toString", "R>R", n_valueToString);
  vm.registerNative("Value.truthy", "R>I", n_valueTruthy);
  vm.registerNative("Value.strictEq", "RR>I", n_strictEq);
  vm.registerNative("console.log", "R>V", n_consoleLog);
  vm.registerNative("console.error", "R>V", n_consoleError);
  vm.registerNative("Global.Number", "R>F", n_number);
  vm.registerNative("Global.parseFloat", "R>F", n_parseFloat);
  vm.registerNative("Global.parseInt", "RF>F", n_parseInt);
  vm.registerNative("Global.encodeURIComponent", "R>R", n_encodeURIComponent);
  vm.registerNative("Global.decodeURIComponent", "R>R", n_decodeURIComponent);
  vm.registerNative("Global.encodeURI", "R>R", n_encodeURI);
  vm.registerNative("Global.decodeURI", "R>R", n_decodeURI);
  vm.registerNative("Number.isInteger", "F>I", n_isInteger);
  vm.registerNative("Number.isSafeInteger", "F>I", n_isSafeInteger);
  vm.registerNative("Number.isFinite", "F>I", n_isFinite);
  vm.registerNative("Number.isNaN", "F>I", n_isNaN);
  vm.registerNative("Number.toStringRadix", "FF>R", n_toStringRadix);
  vm.registerNative("Number.toFixed", "FF>R", n_toFixed);
  vm.registerNative("Number.toPrecision", "FF>R", n_toPrecision);
  vm.registerNative("Number.toExponential", "FF>R", n_toExponential);
  vm.registerNative("Int.toStringRadix", "IFF>R", n_intToStringRadix);
  vm.registerNative("Date.now", ">F", n_dateNow);
  vm.registerNative("Object.keys", "R>R", n_objectKeys);
  vm.registerNative("Text.encode", "R>R", n_textEncode);
  vm.registerNative("Text.decode", "R>R", n_textDecode);
}


// Budget cost models for natives whose work grows with their inputs (see NativeCost in vm.h).
void applyNativeCosts(VM& vm) {
  static const char* scan[] = {
      "Array.indexOfI", "Array.indexOfF", "Array.indexOfR", "Array.lastIndexOfI", "Array.lastIndexOfF",
      "Array.lastIndexOfR", "Array.includesI", "Array.includesF", "Array.includesR", "Array.join", "Array.reverse",
      "Array.fillI", "Array.fillF", "Array.fillR", "Bytes.fromArray", "Bytes.copy", "Bytes.toArray", "Bytes.set",
      "Bytes.fill", "Bytes.indexOf", "Bytes.lastIndexOf", "Bytes.includes", "Bytes.join", "Bytes.reverse",
      "Value.toString", "console.log", "console.error", "Global.Number", "Global.parseFloat", "Global.parseInt",
      "Global.encodeURIComponent", "Global.decodeURIComponent", "Global.encodeURI", "Global.decodeURI",
      "Text.encode", "Text.decode", "Date.fromString", "Date.parse", "encoding.hexEncode", "encoding.hexDecode",
      "encoding.base64Encode", "encoding.base64Decode", "encoding.base64UrlEncode", "encoding.base64UrlDecode",
      "encoding.base32Encode", "encoding.base32Decode", "encoding.utf8Encode", "encoding.utf8Decode",
      "encoding.pemDecode", "encoding.pemEncode", "encoding.concatBytes", "encoding.bytesEqual", "JSON.stringify",
      "JSON.stringifyIndent", "JSON.stringifyIndentStr", "JSON.parse", "JSON.tryParse", "Map.fromEntries", "Set.fromArray",
      "Map.copy", "Math.minArray", "Math.maxArray", "Math.hypotArray", "String.indexOf", "String.lastIndexOf",
      "String.includes", "String.startsWith", "String.endsWith", "String.toUpperCase", "String.toLowerCase",
      "String.trim", "String.trimStart", "String.trimEnd", "String.split", "String.replace", "String.replaceAll",
      "String.localeCompare", "String.fromCharCodes", "String.fromCodePoints", "String.chars",
      "BigInt.fromString", "BigInt.parseLiteral", "BigInt.toNumber", "BigInt.fromNumber", "crypto.timingSafeEqual",
  };
  static const char* out[] = {
      "Array.slice", "Array.splice", "Array.concat", "Array.newLen", "Array.setLength", "Bytes.new", "Bytes.slice",
      "Buffer.new", "Buffer.slice", "Map.keys", "Map.values", "Map.entries", "Object.keys", "String.slice",
      "String.substring", "String.substr", "String.padStart", "String.padEnd", "String.repeat",
      "crypto.randomBytes", "crypto.hkdf",
  };
  static const char* hash[] = {
      "crypto.hash", "crypto.hmac", "crypto.hmacKey", "crypto.crc32", "crypto.aesGcmEncrypt", "crypto.aesGcmDecrypt",
      "crypto.chacha20Poly1305Encrypt", "crypto.chacha20Poly1305Decrypt", "crypto.aesCbcEncrypt",
      "crypto.aesCbcDecrypt", "crypto.aesCtr", "crypto.ed25519Sign", "crypto.ed25519Verify", "crypto.ecdsaSign",
      "crypto.ecdsaVerify", "crypto.rsaVerify", "crypto.rsaSign",
  };
  static const char* bigq[] = {"BigInt.mul", "BigInt.div", "BigInt.mod", "BigInt.pow", "BigInt.toString"};
  for (const char* n : scan) vm.setNativeCost(n, NC_SCAN);
  for (const char* n : out) vm.setNativeCost(n, NC_OUT);
  for (const char* n : hash) vm.setNativeCost(n, NC_HASH);
  for (const char* n : bigq) vm.setNativeCost(n, NC_BIGQ);
  vm.setNativeCost("Array.sort", NC_SORT);
  static const char* hashes[] = {"md5", "sha1", "sha224", "sha256", "sha384", "sha512",
                                 "sha3_224", "sha3_256", "sha3_384", "sha3_512", "keccak256"};
  for (const char* h : hashes) vm.setNativeCost(std::string("crypto.hash.") + h, NC_HASH);
}

}  // namespace e7
