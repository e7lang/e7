// Core natives: value conversion / formatting, console, globals, Number, Text, Object, Date.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <unordered_map>

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
      if (vm.errorParts(o, &en, &em))
        return em->len == 0 ? strU16(en) : en->len == 0 ? strU16(em) : strU16(en) + u": " + strU16(em);
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
      uint32_t n = taLength(b);
      for (uint32_t i = 0; i < n; i++) {
        if (i) out += u',';
        std::string t = taKind(b) == TA_U8 ? std::to_string(b->ptr()[i]) : taElemText(b, i);
        if (taIsBig(taKind(b))) t.pop_back();
        if (t == "-0") t = "0";  // String(-0)
        out += ascii(t);
      }
      return out;
    }
    case OT::Map: return static_cast<MapObj*>(o)->isSet ? u"[object Set]" : u"[object Map]";
    case OT::Instance: {
      auto* in = static_cast<InstObj*>(o);
      if (!in->cls->isTuple) {
        // its own toString(): string (a class's, inherited ones included, or an object literal's), called
        // as JavaScript does, whatever the static type was
        auto it = in->cls->methodIndex.find("toString");
        if (it != in->cls->methodIndex.end()) {
          FuncRT* f = in->cls->vtable[it->second];
          if (f && f->nparams == 1 && f->retKind == 'R') {
            Slot self;
            self.r = o;
            Slot r = vm.callMethod(f, &self, 1);
            if (r.r && r.r->type == OT::String) return strU16(static_cast<StrObj*>(r.r));
            return valueToString(vm, r, VT_REF);
          }
        }
        // { toString: () => "..." }: a property holding a function that returns a string
        auto fi = in->cls->fieldIndex.find("toString");
        if (fi != in->cls->fieldIndex.end() && in->cls->fieldVT[fi->second] == VT_REF && fieldPresent(in, fi->second)) {
          Obj* c = in->fields()[fi->second].r;
          if (c && c->type == OT::Closure) {
            auto* clo = static_cast<CloObj*>(c);
            if (clo->fn && clo->fn->retKind == 'R' && clo->fn->nparams == 0) {
              Slot r = vm.callClosure(clo, nullptr, 0);
              if (r.r && r.r->type == OT::String) return strU16(static_cast<StrObj*>(r.r));
              return valueToString(vm, r, VT_REF);
            }
          }
        }
        return u"[object Object]";
      }
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
    case OT::Promise: return u"[object Promise]";
    case OT::Coro: {
      auto* c = static_cast<CoroObj*>(o);
      return isAsyncGen(c) ? u"[object AsyncGenerator]" : u"[object Generator]";
    }
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

// ------------------------------------------------------------------ util.inspect / console.log
// A port of Node.js util.inspect (lib/internal/util/inspect.js) with the options console.log uses: depth 2,
// breakLength 80, compact 3, maxArrayLength 100, maxStringLength 10000, no colors. The text is built as
// UTF-8; the lengths Node.js measures in UTF-16 code units are computed with len16, display widths (used to
// align grouped array columns) with textWidth.
namespace {

struct WidthRange {
  uint32_t lo, hi;
  uint8_t w;
};
const WidthRange kWidthRanges[] = {
#include "inspect_width.inc"
};

int cpWidth(uint32_t cp) {
  if (cp < 0x7F) return cp >= 0x20 ? 1 : 0;
  size_t lo = 0, hi = sizeof kWidthRanges / sizeof kWidthRanges[0];
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (kWidthRanges[mid].hi < cp) lo = mid + 1;
    else hi = mid;
  }
  return lo < sizeof kWidthRanges / sizeof kWidthRanges[0] && kWidthRanges[lo].lo <= cp ? kWidthRanges[lo].w : 1;
}

// UTF-16 length of UTF-8 text
size_t len16(const std::string& s) {
  size_t n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) n += c >= 0xF0 ? 2 : 1;
  return n;
}

// display width (Node.js getStringWidth, code point by code point)
size_t textWidth(const std::string& s) {
  size_t w = 0;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) {
      w += cpWidth(c);
      i++;
      continue;
    }
    int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    uint32_t cp = c & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
    for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
    w += cpWidth(cp);
    i += n;
  }
  return w;
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) out += (char)cp;
  else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xF0 | (cp >> 18));
    out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

// Node.js strEscape: single quotes unless the text contains one (then double quotes, or backticks when it
// also contains a double quote), escapes for control characters, the quote and the backslash, \uXXXX for
// lone surrogates.
std::string quoteJS(const char16_t* p, size_t n) {
  bool sq = false, dq = false, bt = false;
  for (size_t i = 0; i < n; i++) {
    char16_t c = p[i];
    if (c == '\'') sq = true;
    else if (c == '"') dq = true;
    else if (c == '`' || (c == '$' && i + 1 < n && p[i + 1] == '{')) bt = true;
  }
  char q = '\'';
  if (sq) q = !dq ? '"' : !bt ? '`' : '\'';
  std::string out;
  out.reserve(n + 2);
  out += q;
  char b[16];
  for (size_t i = 0; i < n; i++) {
    char16_t c = p[i];
    if (c < 0x20 || (c >= 0x7F && c <= 0x9F)) {
      switch (c) {
        case '\b': out += "\\b"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\f': out += "\\f"; break;
        case '\r': out += "\\r"; break;
        default: snprintf(b, sizeof b, "\\x%02X", (unsigned)c); out += b;
      }
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c == '\'' && q == '\'') {
      out += "\\'";
    } else if (c < 0x80) {
      out += (char)c;
    } else if (c >= 0xD800 && c <= 0xDFFF) {
      if (c <= 0xDBFF && i + 1 < n && p[i + 1] >= 0xDC00 && p[i + 1] <= 0xDFFF) {
        appendUtf8(out, 0x10000 + ((uint32_t)(c - 0xD800) << 10) + (p[i + 1] - 0xDC00));
        i++;
      } else {
        snprintf(b, sizeof b, "\\u%x", (unsigned)c);
        out += b;
      }
    } else {
      appendUtf8(out, c);
    }
  }
  out += q;
  return out;
}

// property names printed without quotes (Node.js keyStrRegExp: no '$')
bool isPlainKey(const std::string& s) {
  if (s.empty() || !(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
  for (char c : s)
    if (!(isalnum((unsigned char)c) || c == '_')) return false;
  return true;
}

// canonical array index ("0", "17", up to 2^32 - 2): JavaScript lists such keys first, in ascending order
bool arrayIndexKey(const std::string& s, uint64_t& v) {
  if (s.empty() || s.size() > 10 || (s.size() > 1 && s[0] == '0')) return false;
  v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (uint64_t)(c - '0');
  }
  return v <= 0xFFFFFFFEull;
}

std::string formatNumber(double d) {
  if (d == 0 && std::signbit(d)) return "-0";
  return numberToString(d);
}

std::string remainingText(uint64_t n) { return "... " + std::to_string(n) + " more item" + (n > 1 ? "s" : ""); }

std::string functionBase(const CloObj* c) {
  std::string nm = c->fn->name;
  size_t dot = nm.find_last_of('.');
  if (dot != std::string::npos) nm = nm.substr(dot + 1);
  // Node.js names the kind of a generator or async function: [GeneratorFunction: g], [AsyncFunction: f], ...
  static const char* const kKind[] = {"Function", "GeneratorFunction", "AsyncFunction", "AsyncGeneratorFunction"};
  const char* kind = kKind[c->fn->d ? fflagsKind(c->fn->d->fflags) : 0];
  if (nm.empty() || nm[0] == '<') return std::string("[") + kind + " (anonymous)]";
  return std::string("[") + kind + ": " + nm + "]";
}

constexpr uint32_t kMaxArrayLength = 100;
constexpr size_t kMaxStringLength = 10000;
constexpr long kBreakLength = 80;
constexpr int kCompact = 3;

// console.log's conversions charge their work to the instruction budget as they go: a value shared many
// times in an object graph, or a large BigInt (decimal conversion is quadratic in its length), is formatted
// each time it is printed. Same model as BigInt.toString (NC_BIGQ) and the scanning natives (NC_SCAN).
static std::string bigDecimal(VM& vm, const BigObj* b, bool charge) {
  if (charge) {
    uint64_t n = b->mag.size();
    vm.chargeWork(n * n / 4 + n / 2);
  }
  return bigToString(b, 10);
}

struct Inspector {
  VM& vm;
  bool charge = false;      // charge the formatting work to the budget (console.log; not the debugger)
  int depth = 2;            // levels of nested objects shown (deeper ones print as [Array], [Object], ...)
  bool lines = true;        // false: everything on one line, no column grouping (messages, debugger)
  bool showHidden = false;  // console.log's %o
  long indent = 0;          // Node.js ctx.indentationLvl
  int currentDepth = 0;
  std::vector<Obj*> seen;   // objects being formatted (ancestors)
  std::unordered_map<Obj*, size_t> circular;  // targets of [Circular *n] -> n
  size_t produced = 0;      // bytes of text built so far (bounded by the memory limit)

  explicit Inspector(VM& v) : vm(v) {}

  void account(size_t n) {
    produced += n;
    vm.checkTemp(produced);
    if (charge) vm.chargeWork(n / 8);
  }

  std::string value(Slot v, uint8_t vt, int rt, bool typedArray = false) {
    if (vt == VT_NUMBER) return formatNumber(v.f);
    if (vt != VT_REF) return utf16ToUtf8(intToU16(v.i, vt));
    Obj* o = v.r;
    if (!o) return "null";
    if (o == vm.undef()) return "undefined";
    switch (o->type) {
      case OT::String: return string(static_cast<StrObj*>(o));
      case OT::Box: {
        auto* b = static_cast<BoxObj*>(o);
        if (b->flags & 1) return b->v.f != b->v.f ? "Invalid Date" : dateToISO(b->v.f);
        return value(b->v, b->vt, rt, typedArray);
      }
      case OT::BigInt: return bigDecimal(vm, static_cast<BigObj*>(o), charge) + "n";
      case OT::Closure: return functionBase(static_cast<CloObj*>(o));
      case OT::Handle: return "KeyHandle {}";
      case OT::Coro: {
        auto* c = static_cast<CoroObj*>(o);
        return isAsyncGen(c) ? "Object [AsyncGenerator] {}" : "Object [Generator] {}";
      }
      case OT::Promise: {
        auto* p = static_cast<PromiseObj*>(o);
        if (p->state == PS_PENDING) return "Promise { <pending> }";
        if (p->state == PS_REJECTED) {
          StrObj *n, *m;
          std::string e = vm.errorParts(p->value.r, &n, &m)
                              ? (strUtf8(n) + (m->len ? ": " + strUtf8(m) : std::string()))
                              : value(p->value, VT_REF, rt, false);
          return "Promise { <rejected> " + e + " }";
        }
        return "Promise { " + value(p->value, p->vt, rt, false) + " }";
      }
      default: break;
    }
    for (Obj* s : seen)
      if (s == o) {
        auto it = circular.find(o);
        size_t idx = it != circular.end() ? it->second : circular.size() + 1;
        if (it == circular.end()) circular[o] = idx;
        return "[Circular *" + std::to_string(idx) + "]";
      }
    return raw(o, rt, typedArray);
  }

  // Node.js formatPrimitive for strings: truncated after maxStringLength; a long string with line breaks is
  // split after each '\n' into quoted pieces joined with " +".
  std::string string(const StrObj* s) {
    const char16_t* p = s->chars();
    size_t n = s->len;
    std::string trailer;
    if (n > kMaxStringLength) {
      size_t rem = n - kMaxStringLength;
      n = kMaxStringLength;
      trailer = "... " + std::to_string(rem) + " more character" + (rem > 1 ? "s" : "");
    }
    std::string r;
    if (lines && n > 16 && (long)n > kBreakLength - indent - 4) {
      std::string sep = " +\n" + std::string((size_t)(indent + 2), ' ');
      size_t start = 0;
      for (size_t i = 0; i < n; i++)
        if (p[i] == '\n') {
          if (start) r += sep;
          r += quoteJS(p + start, i + 1 - start);
          start = i + 1;
        }
      if (start < n) {
        if (start) r += sep;
        r += quoteJS(p + start, n - start);
      }
    } else {
      r = quoteJS(p, n);
    }
    r += trailer;
    account(r.size());
    return r;
  }

  static bool numericSlot(Slot v, uint8_t vt) {
    if (vt == VT_BOOL) return false;
    if (vt != VT_REF) return true;
    Obj* o = v.r;
    if (!o) return false;
    if (o->type == OT::BigInt) return true;
    if (o->type != OT::Box || (o->flags & 1)) return false;
    auto* b = static_cast<BoxObj*>(o);
    return numericSlot(b->v, b->vt);
  }

  std::string raw(Obj* o, int rt, bool typedArray) {
    StrObj *en, *em;
    if (vm.errorParts(o, &en, &em))  // the compiler rejects printing Errors (Node.js prints a stack trace)
      return em->len ? strUtf8(en) + ": " + strUtf8(em) : strUtf8(en);
    std::string brace0, ctor;
    const char* brace1 = "}";
    bool arrayLike = false;
    std::vector<uint32_t> fields;  // visible fields of an instance, in JavaScript property order
    switch (o->type) {
      case OT::Array: {
        if (static_cast<ArrObj*>(o)->len == 0 && !showHidden) return "[]";
        brace0 = "[", brace1 = "]", ctor = "Array", arrayLike = true;
        break;
      }
      case OT::Bytes: {
        auto* bo = static_cast<BytesObj*>(o);
        uint32_t len = taLength(bo);
        std::string nm = taName(taKind(bo));
        brace0 = nm + "(" + std::to_string(len) + ") [", brace1 = "]", ctor = nm, arrayLike = true;
        if (len == 0 && !showHidden) return brace0 + "]";
        break;
      }
      case OT::Buffer: {
        if (typedArray) return "ArrayBuffer { [byteLength]: " + std::to_string(static_cast<BufObj*>(o)->len) + " }";
        brace0 = "ArrayBuffer {", ctor = "ArrayBuffer";
        break;
      }
      case OT::Map: {
        auto* m = static_cast<MapObj*>(o);
        ctor = m->isSet ? "Set" : "Map";
        brace0 = ctor + "(" + std::to_string(m->count) + ") {";
        if (m->count == 0) return brace0 + "}";
        break;
      }
      case OT::Instance: {
        ClassRT* c = static_cast<InstObj*>(o)->cls;
        if (c->isTuple) {
          if (c->nfields == 0 && !showHidden) return "[]";
          brace0 = "[", brace1 = "]", ctor = "Array", arrayLike = true;
          break;
        }
        bool numericKeys = false;
        for (uint32_t i = 0; i < c->nfields; i++) {
          // a record's missing optional property is not shown; class fields are always own properties
          if (!fieldPresent(static_cast<InstObj*>(o), i)) continue;
          if (privateField(c, i)) continue;  // #private fields are not shown (as in Node.js)
          fields.push_back(i);
          if (!c->fieldNames[i].empty() && c->fieldNames[i][0] >= '0' && c->fieldNames[i][0] <= '9') numericKeys = true;
        }
        if (numericKeys) {
          std::vector<std::pair<uint64_t, uint32_t>> idx;
          std::vector<uint32_t> rest;
          for (uint32_t i : fields) {
            uint64_t k;
            if (arrayIndexKey(c->fieldNames[i], k)) idx.push_back({k, i});
            else rest.push_back(i);
          }
          std::stable_sort(idx.begin(), idx.end(), [](auto& x, auto& y) { return x.first < y.first; });
          fields.clear();
          for (auto& e : idx) fields.push_back(e.second);
          fields.insert(fields.end(), rest.begin(), rest.end());
        }
        std::string prefix = c->isRecord ? "" : c->name + " ";
        if (fields.empty()) return prefix + "{}";
        brace0 = prefix + "{", ctor = c->isRecord ? "Object" : c->name;
        break;
      }
      default: return "[object]";
    }
    if (rt > depth) return "[" + ctor + "]";
    rt++;
    seen.push_back(o);
    currentDepth = rt;
    std::vector<std::string> out;
    bool numeric = false;  // grouped columns are right-aligned when every entry is a number or bigint
    switch (o->type) {
      case OT::Array: {
        auto* a = static_cast<ArrObj*>(o);
        uint32_t shown = std::min(a->len, kMaxArrayLength);
        for (uint32_t i = 0; i < shown; i++) out.push_back(item(a->data[i], a->vt, rt));
        if (a->len > shown) out.push_back(remainingText(a->len - shown));
        if (showHidden) out.push_back("[length]: " + std::to_string(a->len));
        numeric = true;
        for (size_t i = 0; i < out.size() && numeric; i++) numeric = i < a->len && numericSlot(a->data[i], a->vt);
        break;
      }
      case OT::Bytes: {
        auto* b = static_cast<BytesObj*>(o);
        uint32_t n = taLength(b);
        uint32_t shown = std::min(n, kMaxArrayLength);
        for (uint32_t i = 0; i < shown; i++) out.push_back(taKind(b) == TA_U8 ? std::to_string(b->ptr()[i]) : taElemText(b, i));
        if (n > shown) out.push_back(remainingText(n - shown));
        if (showHidden) {
          out.push_back("[BYTES_PER_ELEMENT]: " + std::to_string(taSize(taKind(b))));
          out.push_back("[length]: " + std::to_string(n));
          out.push_back("[byteLength]: " + std::to_string(b->len));
          out.push_back("[byteOffset]: " + std::to_string(b->off));
          indent += 2;
          out.push_back("[buffer]: " + value(slotR(b->buf), VT_REF, rt, true));
          indent -= 2;
        }
        numeric = out.size() <= b->len;
        break;
      }
      case OT::Buffer: {
        auto* b = static_cast<BufObj*>(o);
        static const char* hex = "0123456789abcdef";
        uint32_t shown = std::min(b->len, kMaxArrayLength);
        std::string s = "[Uint8Contents]: <";
        for (uint32_t i = 0; i < shown; i++) {
          if (i) s += ' ';
          s += hex[b->data[i] >> 4];
          s += hex[b->data[i] & 15];
        }
        if (b->len > shown) {
          uint32_t rem = b->len - shown;
          s += " ... " + std::to_string(rem) + " more byte" + (rem > 1 ? "s" : "");
        }
        out.push_back(s + ">");
        out.push_back("[byteLength]: " + std::to_string(b->len));
        break;
      }
      case OT::Map: {
        auto* m = static_cast<MapObj*>(o);
        indent += 2;
        uint32_t i = 0;
        for (size_t k = 0; k < m->entries.size() && i < kMaxArrayLength; k++) {
          const MapEntry& e = m->entries[k];
          if (!e.live) continue;
          i++;
          if (m->isSet) out.push_back(value(e.k, m->kvt, rt));
          else {
            std::string key = value(e.k, m->kvt, rt);
            out.push_back(key + " => " + value(e.v, m->vvt, rt));
          }
        }
        if (m->count > i) out.push_back(remainingText(m->count - i));
        indent -= 2;
        break;
      }
      case OT::Instance: {
        auto* in = static_cast<InstObj*>(o);
        ClassRT* c = in->cls;
        if (c->isTuple) {
          for (uint32_t i = 0; i < c->nfields; i++) out.push_back(item(in->fields()[i], c->fieldVT[i], rt));
          if (showHidden) out.push_back("[length]: " + std::to_string(c->nfields));
          numeric = true;
          for (size_t i = 0; i < out.size() && numeric; i++)
            numeric = i < c->nfields && numericSlot(in->fields()[i], c->fieldVT[i]);
          break;
        }
        for (uint32_t i : fields) {
          const std::string& k = c->fieldNames[i];
          std::u16string k16 = isPlainKey(k) ? std::u16string() : utf8ToUtf16(k);
          std::string name = isPlainKey(k) ? k : quoteJS(k16.data(), k16.size());
          if (uint8_t ak = accessorKind(c, i)) {  // an accessor property is not read (Node.js)
            out.push_back(name + ": " + (ak == 3 ? "[Getter/Setter]" : ak == 1 ? "[Getter]" : "[Setter]"));
            continue;
          }
          out.push_back(name + ": " + item(in->fields()[i], c->fieldVT[i], rt));
        }
        break;
      }
      default: break;
    }
    seen.pop_back();
    std::string base;
    auto it = circular.find(o);
    if (it != circular.end()) base = "<ref *" + std::to_string(it->second) + ">";
    std::string res = reduce(out, base, brace0, brace1, arrayLike, rt, numeric);
    account(res.size());
    return res;
  }

  // an array element or property value (Node.js formatProperty: two more columns of indentation)
  std::string item(Slot v, uint8_t vt, int rt) {
    indent += 2;
    std::string s = value(v, vt, rt);
    indent -= 2;
    return s;
  }

  // Node.js groupArrayElements: more than 6 short entries are laid out in aligned columns
  std::vector<std::string> group(std::vector<std::string>& out, bool numeric) {
    size_t total = 0, maxLen = 0, outLen = out.size();
    if (kMaxArrayLength < out.size()) outLen--;  // the "... more items" entry
    const size_t sep = 2;
    std::vector<size_t> dataLen(outLen);
    for (size_t i = 0; i < outLen; i++) {
      size_t len = textWidth(out[i]);
      dataLen[i] = len;
      total += len + sep;
      if (maxLen < len) maxLen = len;
    }
    size_t actualMax = maxLen + sep;
    std::vector<std::string> tmp;
    if ((long)(actualMax * 3) + indent < kBreakLength && ((double)total / (double)actualMax > 5 || maxLen <= 6)) {
      double averageBias = std::sqrt((double)actualMax - (double)total / (double)out.size());
      double biasedMax = std::max((double)actualMax - 3 - averageBias, 1.0);
      double columns = std::min({std::floor(std::sqrt(2.5 * biasedMax * (double)outLen) / biasedMax + 0.5),
                                 std::floor((double)(kBreakLength - indent) / (double)actualMax),
                                 (double)kCompact * 4, 15.0});
      if (columns <= 1) return std::move(out);
      size_t cols = (size_t)columns;
      std::vector<size_t> maxLineLength(cols);
      for (size_t i = 0; i < cols; i++) {
        size_t lineMax = 0;
        for (size_t j = i; j < outLen; j += cols)
          if (dataLen[j] > lineMax) lineMax = dataLen[j];
        maxLineLength[i] = lineMax + sep;
      }
      for (size_t i = 0; i < outLen; i += cols) {
        size_t max = std::min(i + cols, outLen);
        std::string str;
        size_t j = i;
        for (; j < max - 1; j++) {
          std::string cell = out[j] + ", ";
          size_t target = maxLineLength[j - i] + len16(out[j]) - dataLen[j], have = len16(cell);
          if (have < target) {
            if (numeric) cell.insert(0, target - have, ' ');
            else cell.append(target - have, ' ');
          }
          str += cell;
        }
        if (numeric) {
          size_t target = maxLineLength[j - i] + len16(out[j]) - dataLen[j] - sep, have = len16(out[j]);
          if (have < target) str.append(target - have, ' ');
        }
        str += out[j];
        tmp.push_back(std::move(str));
      }
      if (kMaxArrayLength < out.size()) tmp.push_back(out[outLen]);
      return tmp;
    }
    return std::move(out);
  }

  bool belowBreakLength(const std::vector<std::string>& out, size_t start, const std::string& base) {
    size_t total = out.size() + start;
    if (total + out.size() > (size_t)kBreakLength) return false;
    for (const std::string& s : out) {
      total += len16(s);
      if (total > (size_t)kBreakLength) return false;
    }
    return base.find('\n') == std::string::npos;
  }

  // Node.js reduceToSingleString: one line when the entries fit in breakLength, otherwise one entry per line
  std::string reduce(std::vector<std::string>& out, const std::string& base, const std::string& brace0,
                     const char* brace1, bool arrayLike, int rt, bool numeric) {
    std::string head = base.empty() ? brace0 : base + " " + brace0;
    if (lines) {
      size_t entries = out.size();
      if (arrayLike && entries > 6) out = group(out, numeric);
      if (currentDepth - rt < kCompact && entries == out.size()) {
        size_t start = out.size() + (size_t)indent + len16(brace0) + len16(base) + 10;
        if (belowBreakLength(out, start, base)) {
          std::string joined = join(out, ", ");
          if (joined.find('\n') == std::string::npos) return head + " " + joined + " " + brace1;
        }
      }
    } else {
      return head + " " + join(out, ", ") + " " + brace1;
    }
    std::string ind = "\n" + std::string((size_t)indent, ' ');
    return head + ind + "  " + join(out, "," + ind + "  ") + ind + brace1;
  }

  static std::string join(const std::vector<std::string>& v, const std::string& sep) {
    size_t n = 0;
    for (auto& s : v) n += s.size() + sep.size();
    std::string r;
    r.reserve(n);
    for (size_t i = 0; i < v.size(); i++) {
      if (i) r += sep;
      r += v[i];
    }
    return r;
  }
};

std::string inspectArg(VM& vm, Slot v, uint8_t vt, int depth, bool hidden) {
  Inspector in(vm);
  in.charge = true;
  in.depth = depth;
  in.showHidden = hidden;
  return in.value(v, vt, 0);
}

// unwrap boxed primitives (nullable numbers / integers)
void unbox(Slot& v, uint8_t& vt) {
  while (vt == VT_REF && v.r && v.r->type == OT::Box && !(v.r->flags & 1)) {
    auto* b = static_cast<BoxObj*>(v.r);
    v = b->v;
    vt = b->vt;
  }
}

// JavaScript Number(x)
double toNumberJS(VM& vm, Slot v, uint8_t vt) {
  if (vt == VT_NUMBER) return v.f;
  if (vt == VT_U64) return (double)(uint64_t)v.i;
  if (vt != VT_REF) return (double)v.i;
  Obj* o = v.r;
  if (!o) return 0;
  if (o == vm.undef()) return NAN;
  switch (o->type) {
    case OT::String: return stringToNumber(strU16(static_cast<StrObj*>(o)));
    case OT::Box: return static_cast<BoxObj*>(o)->v.f;  // a Date (unboxed before otherwise)
    case OT::BigInt: return bigToDouble(static_cast<BigObj*>(o));
    case OT::Array:
    case OT::Bytes: return stringToNumber(valueToString(vm, v, vt));
    case OT::Instance:
      if (static_cast<InstObj*>(o)->cls->isTuple) return stringToNumber(valueToString(vm, v, vt));
      return NAN;
    default: return NAN;
  }
}

bool hasToStringMethod(const ClassRT* c) {
  for (; c; c = c->parent)
    if (c->methodIndex.count("toString")) return true;
  return false;
}

// one console.log substitution (%s %d %i %f %j %o %O)
std::string substitute(VM& vm, char16_t conv, Slot v, uint8_t vt) {
  unbox(v, vt);
  bool big = vt == VT_REF && v.r && v.r->type == OT::BigInt;
  switch (conv) {
    case 's': {
      if (vt == VT_NUMBER) return formatNumber(v.f);
      if (big) return bigDecimal(vm, static_cast<BigObj*>(v.r), true) + "n";
      Obj* o = vt == VT_REF ? v.r : nullptr;
      // String(x) unless x has the toString of a global built-in (Node.js hasBuiltInToString): Uint8Array's
      // comes from %TypedArray%, a class's own toString method is its own
      if (!o || o == vm.undef() || o->type == OT::String || o->type == OT::Closure || o->type == OT::Bytes ||
          (o->type == OT::Instance && hasToStringMethod(static_cast<InstObj*>(o)->cls)))
        return utf16ToUtf8(valueToString(vm, v, vt));
      return inspectArg(vm, v, vt, 0, false);
    }
    case 'd':
      if (big) return bigDecimal(vm, static_cast<BigObj*>(v.r), true) + "n";
      return formatNumber(toNumberJS(vm, v, vt));
    case 'i':
      if (big) return bigDecimal(vm, static_cast<BigObj*>(v.r), true) + "n";
      return formatNumber(parseIntJS(valueToString(vm, v, vt), 0));
    case 'f': return formatNumber(parseFloatJS(valueToString(vm, v, vt)));
    case 'j': return jsonForLog(vm, v, vt);
    case 'O': return inspectArg(vm, v, vt, 2, false);
    default: return inspectArg(vm, v, vt, 4, true);  // 'o'
  }
}

// console.log arguments (Node.js formatWithOptions): printf-style substitutions in a first string argument,
// other strings as they are, other values inspected, separated by spaces
std::string formatLog(VM& vm, ArrObj* a) {
  std::string str;
  uint32_t i = 0;
  bool join = false;
  auto isString = [&](uint32_t k) { return a->vt == VT_REF && a->data[k].r && a->data[k].r->type == OT::String; };
  if (a->len > 0 && isString(0)) {
    auto* first = static_cast<StrObj*>(a->data[0].r);
    if (a->len == 1) return strUtf8(first);
    const char16_t* p = first->chars();
    size_t n = first->len, lastPos = 0;
    for (size_t k = 0; k + 1 < n; k++) {
      if (p[k] != '%') continue;
      char16_t next = p[++k];
      if (i + 1 != a->len) {
        std::string tmp;
        switch (next) {
          case 's': case 'd': case 'i': case 'f': case 'j': case 'o': case 'O':
            i++;
            tmp = substitute(vm, next, a->data[i], a->vt);
            break;
          case 'c': i++; break;
          case '%':
            str += utf16ToUtf8(p + lastPos, k - lastPos);
            lastPos = k + 1;
            continue;
          default: continue;
        }
        if (lastPos != k - 1) str += utf16ToUtf8(p + lastPos, k - 1 - lastPos);
        str += tmp;
        lastPos = k + 1;
        vm.checkTemp(str.size());
      } else if (next == '%') {
        str += utf16ToUtf8(p + lastPos, k - lastPos);
        lastPos = k + 1;
      }
    }
    if (lastPos != 0) {
      i++;
      join = true;
      if (lastPos < n) str += utf16ToUtf8(p + lastPos, n - lastPos);
    }
  }
  for (; i < a->len; i++) {
    if (join) str += ' ';
    str += isString(i) ? strUtf8(static_cast<StrObj*>(a->data[i].r)) : inspectArg(vm, a->data[i], a->vt, 2, false);
    join = true;
    vm.checkTemp(str.size());
  }
  return str;
}

}  // namespace

bool accessorGet(VM& vm, InstObj* in, uint32_t i, Slot* out, uint8_t* vt) {
  // field i holds the accessor pair [getter, setter, value slot] (checked: the bytecode is untrusted)
  Obj* p = i < in->cls->nfields && in->cls->fieldVT[i] == VT_REF ? in->fields()[i].r : nullptr;
  if (!p || p->type != OT::Instance) vm.error("malformed accessor property");
  auto* pair = static_cast<InstObj*>(p);
  const ClassRT* pc = pair->cls;
  if (!pc->isTuple || pc->nfields != 3 || pc->fieldVT[0] != VT_REF) vm.error("malformed accessor property");
  Obj* g = pair->fields()[0].r;
  if (!g) return false;  // a setter only
  if (g->type != OT::Closure) vm.error("malformed accessor property");
  auto* c = static_cast<CloObj*>(g);
  uint8_t rvt = pc->fieldVT[2];
  if (c->fn->nparams != 0 || c->fn->retKind != kindChar(kindOfVT(rvt))) vm.error("malformed accessor property");
  VM::TempRoot keep(vm, in);
  *out = vm.callClosure(c, nullptr, 0);
  *vt = rvt;
  return true;
}

// Field indices of a class / record in JavaScript property order: array-index keys ascending first, then the
// others in definition order (Object.keys, JSON.stringify, console.log).
std::vector<uint32_t> jsFieldOrder(const ClassRT* c) {
  std::vector<std::pair<uint64_t, uint32_t>> idx;
  std::vector<uint32_t> out;
  out.reserve(c->nfields);
  for (uint32_t i = 0; i < c->nfields; i++) {
    uint64_t k;
    if (arrayIndexKey(c->fieldNames[i], k)) idx.push_back({k, i});
    else out.push_back(i);
  }
  if (idx.empty()) return out;
  std::stable_sort(idx.begin(), idx.end(), [](const std::pair<uint64_t, uint32_t>& x, const std::pair<uint64_t, uint32_t>& y) {
    return x.first < y.first;
  });
  std::vector<uint32_t> order;
  order.reserve(c->nfields);
  for (auto& e : idx) order.push_back(e.second);
  order.insert(order.end(), out.begin(), out.end());
  return order;
}


// one value on a single line (assertion messages, debugger): `depth` nested levels are already used up
std::string inspectValue(VM& vm, Slot v, uint8_t vt, int depth, bool top) {
  if (top && vt == VT_REF && v.r && v.r->type == OT::String) return strUtf8(static_cast<StrObj*>(v.r));
  Inspector in(vm);
  in.depth = 2 - depth;
  in.lines = false;
  return in.value(v, vt, 0);
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
  std::string line = formatLog(vm, A(vm, arr));
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
  if (rd < 2 || rd > 36) vm.error("RangeError: toString() radix argument must be between 2 and 36");
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
  if (rd < 2 || rd > 36) vm.error("RangeError: toString() radix argument must be between 2 and 36");
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
  VM::TempRoot keep(vm, arr);
  for (uint32_t i : jsFieldOrder(in->cls)) {
    // a record's missing optional property is skipped; the fields of a class instance are always own
    // properties (also when undefined), as in JavaScript
    if (!fieldPresent(in, i)) continue;
    if (privateField(in->cls, i)) continue;
    vm.arrayPush(arr, slotR(vm.newStringUtf8(in->cls->fieldNames[i])));
  }
  r->r = arr;
}

// Object.has(obj, name): whether the object has an own property of that name (fields of class
// instances always; record fields unless absent, D7). Used by object spread.
E7_NATIVE(n_objectHas) {
  (void)n;
  (void)ud;
  Obj* o = a[0].r;
  r->i = 0;
  if (!o || o->type != OT::Instance) return;
  auto* in = static_cast<InstObj*>(o);
  auto it = in->cls->fieldIndex.find(strUtf8(asStr(vm, a[1])));
  if (it != in->cls->fieldIndex.end()) r->i = fieldPresent(in, it->second);
}

// structuredClone(v): a deep copy that keeps shared and cyclic references (the compiler rejects values
// it cannot copy: class instances, functions, ...)
namespace {
struct Cloner {
  VM& vm;
  std::unordered_map<Obj*, Obj*> done;
  std::vector<Obj*> roots;  // the copies (kept reachable while copying)
  int depth = 0;            // nesting (the C++ stack), bounded as JSON's
  struct Deeper {
    int& d;
    explicit Deeper(int& x) : d(x) { d++; }
    ~Deeper() { d--; }
  };
  Obj* copy(Obj* o) {
    if (!o || o == vm.undef()) return o;
    auto it = done.find(o);
    if (it != done.end()) return it->second;
    Deeper deeper(depth);
    if (depth > 256) vm.error("DataCloneError: the value is nested too deeply to clone (more than 256 levels)");
    vm.chargeWork(1);
    switch (o->type) {
      case OT::String: case OT::BigInt: return o;  // immutable
      case OT::Box: {
        auto* b = static_cast<BoxObj*>(o);
        BoxObj* c = vm.newBox(b->vt, b->v);
        c->flags = b->flags;  // a Date
        return keep(o, c);
      }
      case OT::Array: {
        auto* a = static_cast<ArrObj*>(o);
        ArrObj* c = vm.newArray(a->vt, a->len);
        keep(o, c);
        c->flags = a->flags;  // an Error
        for (uint32_t i = 0; i < a->len; i++) {
          Slot v = a->data[i];
          if (a->vt == VT_REF) v.r = copy(v.r);
          c->data[i] = v;
          c->len = i + 1;
        }
        StrObj *en, *em;
        if (vm.errorParts(c, &en, &em)) {
          // an Error's copy is of the class its name says when that is a standard one, else an Error (the
          // HTML structured clone algorithm), without the properties assigned to the original
          static const char16_t* const std16[] = {u"Error", u"EvalError", u"RangeError", u"ReferenceError",
                                                   u"SyntaxError", u"TypeError", u"URIError"};
          std::u16string nm = strU16(en);
          bool standard = false;
          for (const char16_t* x : std16) standard = standard || nm == x;
          if (!standard) c->data[0].r = vm.newStringAscii("Error");
          c->flags = kArrFlagError;
        }
        return c;
      }
      case OT::Instance: {
        auto* in = static_cast<InstObj*>(o);
        if (!in->cls->isRecord) vm.error("DataCloneError: a class instance cannot be cloned");
        if (!in->cls->accessors.empty())  // the copy would need a class with data properties instead
          vm.error("structuredClone of an object with getters or setters is not supported (an e7 limit)");
        InstObj* c = vm.newInstance(in->cls);
        keep(o, c);
        if (in->cls->presence) memcpy(c->mask(), in->mask(), presenceWords(in->cls->nfields) * 8);
        for (uint32_t i = 0; i < in->cls->nfields; i++) {
          Slot v = in->fields()[i];
          if (in->cls->fieldVT[i] == VT_REF) v.r = copy(v.r);
          c->fields()[i] = v;
        }
        return c;
      }
      case OT::Bytes: {
        auto* b = static_cast<BytesObj*>(o);
        BytesObj* c = vm.newBytesFrom(b->ptr(), b->len);
        c->flags = b->flags;  // the element kind of a typed array
        return keep(o, c);
      }
      case OT::Buffer: {
        auto* b = static_cast<BufObj*>(o);
        BufObj* c = vm.newBuffer(b->len);
        if (b->len) memcpy(c->data, b->data, b->len);
        return keep(o, c);
      }
      case OT::Map: {
        auto* m = static_cast<MapObj*>(o);
        MapObj* c = vm.newMap(m->kvt, m->vvt, m->km, m->isSet);
        keep(o, c);
        vm.accountExternal((int64_t)(m->entries.capacity() * sizeof(MapEntry) + m->buckets.capacity() * 4));
        c->entries = m->entries;
        c->buckets = m->buckets;
        c->count = m->count;
        c->nextSeq = m->nextSeq;
        for (auto& e : c->entries) {
          if (!e.live) continue;
          if (c->kvt == VT_REF) e.k.r = copy(e.k.r);
          if (c->vvt == VT_REF) e.v.r = copy(e.v.r);
        }
        return c;
      }
      default:
        vm.error("DataCloneError: the value cannot be cloned");
    }
  }
  Obj* keep(Obj* from, Obj* to) {
    done[from] = to;
    roots.push_back(to);
    return to;
  }
};
}  // namespace
E7_NATIVE(n_structuredClone) {
  E7_UNUSED;
  Cloner c{vm, {}, {}};
  VM::GCPause noCollect(vm);  // the copies live in C++ only until the result is returned
  r->r = c.copy(a[0].r);
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
// new Error(message, { cause }): [name, message, cause]
E7_NATIVE(n_errorNewCause) {
  E7_UNUSED;
  StrObj* name = S(vm, a[0]);
  StrObj* msg = S(vm, a[1]);
  ArrObj* e = vm.newArray(VT_REF, 3);
  e->data[0].r = name;
  e->data[1].r = msg;
  e->data[2].r = a[2].r ? a[2].r : vm.undef();
  e->len = 3;
  e->flags |= kArrFlagError;
  r->r = e;
}
// new AggregateError(errors, message): [name, message, undefined, errors (a copy)]
E7_NATIVE(n_errorNewAggregate) {
  E7_UNUSED;
  ArrObj* src = A(vm, a[0]);
  if (src->vt != VT_REF) vm.error("AggregateError: expected an array of errors");
  StrObj* msg = S(vm, a[1]);
  ArrObj* errs = vm.newArray(VT_REF, src->len);
  VM::TempRoot keep(vm, errs);
  if (src->len) memcpy(errs->data, src->data, (size_t)src->len * sizeof(Slot));
  errs->len = src->len;
  StrObj* name = vm.newStringAscii("AggregateError");
  VM::TempRoot keepN(vm, name);
  ArrObj* e = vm.newArray(VT_REF, 4);
  e->data[0].r = name;
  e->data[1].r = msg;
  e->data[2].r = vm.undef();
  e->data[3].r = errs;
  e->len = 4;
  e->flags |= kArrFlagError;
  r->r = e;
}
E7_NATIVE(n_errorCause) {
  E7_UNUSED;
  StrObj *nm, *m;
  errParts(vm, a[0], &nm, &m);
  auto* e = static_cast<ArrObj*>(a[0].r);
  r->r = e->len >= 3 && e->data[2].r ? e->data[2].r : vm.undef();
}
E7_NATIVE(n_errorErrors) {
  E7_UNUSED;
  StrObj *nm, *m;
  errParts(vm, a[0], &nm, &m);
  auto* e = static_cast<ArrObj*>(a[0].r);
  if (e->len >= 4 && e->data[3].r && e->data[3].r->type == OT::Array) { r->r = e->data[3].r; return; }
  r->r = vm.newArray(VT_REF, 0);  // not an AggregateError
}
E7_NATIVE(n_errorName) { E7_UNUSED; StrObj *nm, *m; errParts(vm, a[0], &nm, &m); r->r = nm; }
E7_NATIVE(n_errorMessage) { E7_UNUSED; StrObj *nm, *m; errParts(vm, a[0], &nm, &m); r->r = m; }
// e.message = s / e.name = s (0.1.3): the value assigned
// The error classes, for instanceof after e.name was changed (the class is kept in Obj::flags bits 8-11,
// its index + 1, set by the first change of the name)
static const char16_t* const kErrClasses[] = {u"Error", u"TypeError", u"RangeError", u"SyntaxError", u"URIError",
                                              u"AggregateError", u"ReferenceError", u"EvalError"};
static void errSet(VM& vm, Slot* a, Slot* r, int part) {
  StrObj *nm, *m;
  errParts(vm, a[0], &nm, &m);
  auto* arr = static_cast<ArrObj*>(a[0].r);
  if (part == 0 && ((arr->flags >> 8) & 0xF) == 0) {
    std::u16string cur = strU16(nm);
    uint16_t k = 1;  // (a name that is not a class's: an Error)
    for (uint16_t i = 0; i < 8; i++)
      if (cur == kErrClasses[i]) k = (uint16_t)(i + 1);
    arr->flags = (uint16_t)(arr->flags | (k << 8));
  }
  // an own (enumerable) property now, as in JavaScript: the name, or a message that the error did not have
  if (part == 0) arr->flags |= 0x1000;
  else if (m->len == 0) arr->flags |= 0x2000;
  arr->data[part].r = S(vm, a[1]);
  r->r = a[1].r;
}
E7_NATIVE(n_errorSetName) { E7_UNUSED; errSet(vm, a, r, 0); }
E7_NATIVE(n_errorSetMessage) { E7_UNUSED; errSet(vm, a, r, 1); }
E7_NATIVE(n_errorToString) {  // String(e): "Name: message", or "Name" when the message is empty
  E7_UNUSED;
  StrObj *nm, *m;
  errParts(vm, a[0], &nm, &m);
  if (m->len == 0) { r->r = nm; return; }
  if (nm->len == 0) { r->r = m; return; }  // (an empty name: the message alone)
  std::u16string s = strU16(nm) + u": " + strU16(m);
  r->r = vm.newString(s);
}
E7_NATIVE(n_errorIs) {  // x instanceof Error / TypeError / ...: false for anything that is not an Error
  E7_UNUSED;
  StrObj *nm, *m;
  std::u16string want = strU16(S(vm, a[1]));
  if (!vm.errorParts(a[0].r, &nm, &m)) { r->i = 0; return; }
  uint16_t k = (a[0].r->flags >> 8) & 0xF;  // the class when the name was changed
  std::u16string cls = k >= 1 && k <= 8 ? std::u16string(kErrClasses[k - 1]) : strU16(nm);
  r->i = (want == u"Error" || cls == want) ? 1 : 0;
}

void registerCoreNatives(VM& vm) {
  vm.registerNative("Error.new", "RR>R", n_errorNew);
  vm.registerNative("Error.newCause", "RRR>R", n_errorNewCause);
  vm.registerNative("Error.newAggregate", "RR>R", n_errorNewAggregate);
  vm.registerNative("Error.cause", "R>R", n_errorCause);
  vm.registerNative("Error.errors", "R>R", n_errorErrors);
  vm.registerNative("Error.name", "R>R", n_errorName);
  vm.registerNative("Error.message", "R>R", n_errorMessage);
  vm.registerNative("Error.setName", "RR>R", n_errorSetName);
  vm.registerNative("Error.setMessage", "RR>R", n_errorSetMessage);
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
  vm.registerNative("Object.has", "RR>I", n_objectHas);
  vm.registerNative("Global.structuredClone", "R>R", n_structuredClone);
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
      "JSON.stringifyIndent", "JSON.stringifyIndentStr", "JSON.parse", "JSON.tryParse", "JSON.parse2", "JSON.tryParse2", "Map.fromEntries", "Set.fromArray",
      "Map.copy", "Math.minArray", "Math.maxArray", "Math.hypotArray", "String.indexOf", "String.lastIndexOf",
      "String.includes", "String.startsWith", "String.endsWith", "String.toUpperCase", "String.toLowerCase",
      "String.trim", "String.trimStart", "String.trimEnd", "String.split", "String.replace", "String.replaceAll",
      "String.localeCompare", "String.fromCharCodes", "String.fromCodePoints", "String.chars",
      "BigInt.fromString", "BigInt.parseLiteral", "BigInt.toNumber", "BigInt.fromNumber", "crypto.timingSafeEqual",
  };
  static const char* out[] = {
      "Array.slice", "Array.splice", "Array.concat", "Array.newLen", "Array.setLength", "Bytes.new", "Bytes.slice",
      "Array.toReversed", "Array.toSpliced", "Array.withI", "Array.withF", "Array.withR",
      "Buffer.new", "Buffer.slice", "Map.keys", "Map.values", "Map.entries", "Object.keys", "String.slice",
      "String.substring", "String.substr", "String.padStart", "String.padEnd", "String.repeat",
      "crypto.randomBytes", "crypto.hkdf",
  };
  static const char* hash[] = {
      "crypto.hash", "crypto.hmac", "crypto.hmacKey", "encoding.crc32", "crypto.aesGcmEncrypt", "crypto.aesGcmDecrypt",
      "crypto.chacha20Poly1305Encrypt", "crypto.chacha20Poly1305Decrypt", "crypto.ed25519Sign", "crypto.ed25519Verify",
  };
  static const char* bigq[] = {"BigInt.mul", "BigInt.div", "BigInt.mod", "BigInt.pow", "BigInt.toString"};
  for (const char* n : scan) vm.setNativeCost(n, NC_SCAN);
  for (const char* n : out) vm.setNativeCost(n, NC_OUT);
  for (const char* n : hash) vm.setNativeCost(n, NC_HASH);
  for (const char* n : bigq) vm.setNativeCost(n, NC_BIGQ);
  vm.setNativeCost("Array.sort", NC_SORT);
  vm.setNativeCost("Array.toSorted", NC_SORT);
  static const char* hashes[] = {"sha256", "sha384", "sha512"};
  for (const char* h : hashes) vm.setNativeCost(std::string("crypto.hash.") + h, NC_HASH);
}

}  // namespace e7
