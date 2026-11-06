// JSON.stringify (JavaScript semantics) and typed JSON.parse (validating, descriptor-driven).
#include <cstdlib>
#include <algorithm>
#include <unordered_set>

#include "util.h"

namespace e7 {

std::string dateToISO(double t);

// ------------------------------------------------------------------ stringify
static void quoteJson(std::u16string& out, const char16_t* s, size_t n) {
  static const char* hex = "0123456789abcdef";
  out += u'"';
  for (size_t i = 0; i < n; i++) {
    char16_t c = s[i];
    switch (c) {
      case '"': out += u"\\\""; break;
      case '\\': out += u"\\\\"; break;
      case '\b': out += u"\\b"; break;
      case '\f': out += u"\\f"; break;
      case '\n': out += u"\\n"; break;
      case '\r': out += u"\\r"; break;
      case '\t': out += u"\\t"; break;
      default: {
        bool lone = false;
        if (c >= 0xD800 && c <= 0xDBFF) lone = !(i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF);
        else if (c >= 0xDC00 && c <= 0xDFFF) lone = !(i > 0 && s[i - 1] >= 0xD800 && s[i - 1] <= 0xDBFF);
        if (c < 0x20 || lone) {
          out += u"\\u";
          for (int k = 3; k >= 0; k--) out += (char16_t)hex[(c >> (4 * k)) & 15];
        } else {
          out += c;
        }
      }
    }
  }
  out += u'"';
}

struct Stringifier {
  VM& vm;
  std::u16string gap;
  std::u16string out;
  std::unordered_set<Obj*> stack;
  int depth = 0;
  bool cycleOk = false;  // console.log's %j: a cycle sets `cycle` instead of raising a TypeError
  bool cycle = false;
  size_t nextCheck = 0;  // out.size() at which the output is next checked against the memory limit
  bool charge = false;   // console.log's %j: charge the output to the budget as it grows (JSON.stringify's
  size_t charged = 0;    // cost is charged after the call, from the result)

  // The output is a transient buffer: bounded by the memory limit like the heap (a value shared many
  // times in an object graph is written out each time, so the output can be far larger than the heap)
  void checkOut() {
    if (out.size() < nextCheck) return;
    vm.checkTemp(out.capacity() * sizeof(char16_t));
    if (charge) {
      vm.chargeWork((out.size() - charged) / 8);
      charged = out.size();
    }
    nextCheck = out.size() + 16384;
  }

  void nl(const std::u16string& indent) {
    if (gap.empty()) return;
    out += u'\n';
    out += indent;
  }
  // returns false if the value is "undefined" (omitted)
  bool value(Slot v, uint8_t vt, const std::u16string& indent) {
    checkOut();
    if (vt == VT_NUMBER) {
      if (!std::isfinite(v.f)) { out += u"null"; return true; }
      std::string s = numberToString(v.f);
      out.append(s.begin(), s.end());
      return true;
    }
    if (vt != VT_REF) {
      out += intToU16(v.i, vt);
      return true;
    }
    Obj* o = v.r;
    if (!o) { out += u"null"; return true; }
    if (o == vm.undef()) return false;
    switch (o->type) {
      case OT::String: {
        auto* s = static_cast<StrObj*>(o);
        quoteJson(out, s->chars(), s->len);
        return true;
      }
      case OT::Box: {
        auto* b = static_cast<BoxObj*>(o);
        if (b->flags & 1) {  // Date.prototype.toJSON
          if (b->v.f != b->v.f) { out += u"null"; return true; }
          std::string iso = dateToISO(b->v.f);
          out += u'"';
          out.append(iso.begin(), iso.end());
          out += u'"';
          return true;
        }
        return value(b->v, b->vt, indent);
      }
      case OT::BigInt: vm.error("TypeError: Do not know how to serialize a BigInt");
      case OT::Closure: return false;
      default: break;
    }
    if (stack.count(o)) {
      if (!cycleOk) vm.error("TypeError: Converting circular structure to JSON");
      cycle = true;
      return true;
    }
    if (++depth > 256) vm.error("JSON.stringify: nesting deeper than 256 levels (an e7 limit)");
    stack.insert(o);
    std::u16string inner = indent + gap;
    StrObj *en, *em;
    if (vm.errorParts(o, &en, &em)) {  // an Error has no enumerable properties: "{}" as in JavaScript
      out += u"{}";
      stack.erase(o);
      depth--;
      return true;
    }
    if (o->type == OT::Array) {
      auto* a = static_cast<ArrObj*>(o);
      if (a->len == 0) out += u"[]";
      else {
        out += u'[';
        for (uint32_t i = 0; i < a->len; i++) {
          if (i) out += u',';
          nl(inner);
          if (!value(a->data[i], a->vt, inner)) out += u"null";
        }
        nl(indent);
        out += u']';
      }
    } else if (o->type == OT::Instance && static_cast<InstObj*>(o)->cls->isTuple) {
      auto* in = static_cast<InstObj*>(o);
      out += u'[';
      for (uint32_t i = 0; i < in->cls->nfields; i++) {
        if (i) out += u',';
        nl(inner);
        if (!value(in->fields()[i], in->cls->fieldVT[i], inner)) out += u"null";
      }
      if (in->cls->nfields) nl(indent);
      out += u']';
    } else if (o->type == OT::Instance) {
      auto* in = static_cast<InstObj*>(o);
      out += u'{';
      bool any = false;
      for (uint32_t i : jsFieldOrder(in->cls)) {
        Slot fv = in->fields()[i];
        uint8_t fvt = in->cls->fieldVT[i];
        if (fvt == VT_REF && (fv.r == vm.undef() || (fv.r && fv.r->type == OT::Closure))) continue;
        if (privateField(in->cls, i)) continue;  // #private fields are not serialized
        if (any) out += u',';
        nl(inner);
        std::u16string key = utf8ToUtf16(in->cls->fieldNames[i]);
        quoteJson(out, key.data(), key.size());
        out += gap.empty() ? u":" : u": ";
        value(fv, fvt, inner);
        any = true;
      }
      if (any) nl(indent);
      out += u'}';
    } else if (o->type == OT::Bytes) {
      auto* b = static_cast<BytesObj*>(o);
      out += u'{';
      for (uint32_t i = 0; i < b->len; i++) {
        if (i) out += u',';
        nl(inner);
        std::string k = "\"" + std::to_string(i) + (gap.empty() ? "\":" : "\": ") + std::to_string(b->ptr()[i]);
        out.append(k.begin(), k.end());
        checkOut();
      }
      if (b->len) nl(indent);
      out += u'}';
    } else {
      out += u"{}";
    }
    stack.erase(o);
    depth--;
    checkOut();
    if (out.size() > 0x3FFFFFFF) vm.error("RangeError: invalid string length");
    return true;
  }
};

static void stringifyImpl(VM& vm, Slot v, const std::u16string& gap, Slot* r) {
  Stringifier s{vm, gap, {}, {}, 0};
  if (!s.value(v, VT_REF, u"")) { r->r = vm.newStringAscii("undefined"); return; }
  r->r = vm.newString(s.out);
}
std::string jsonForLog(VM& vm, Slot v, uint8_t vt) {
  Stringifier s{vm, u"", {}, {}, 0};
  s.cycleOk = true;
  s.charge = true;
  if (!s.value(v, vt, u"")) return "undefined";
  s.nextCheck = 0;
  s.checkOut();
  return s.cycle ? "[Circular]" : utf16ToUtf8(s.out);
}
E7_NATIVE(j_stringify) { E7_UNUSED; stringifyImpl(vm, a[0], u"", r); }
E7_NATIVE(j_stringifyIndent) {
  E7_UNUSED;
  double n0 = toInteger(a[1].f);
  if (n0 > 10) n0 = 10;
  std::u16string gap(n0 > 0 ? (size_t)n0 : 0, u' ');
  stringifyImpl(vm, a[0], gap, r);
}
E7_NATIVE(j_stringifyIndentStr) {
  E7_UNUSED;
  StrObj* g = S(vm, a[1]);
  std::u16string gap(g->chars(), std::min<uint32_t>(g->len, 10));
  stringifyImpl(vm, a[0], gap, r);
}

// ------------------------------------------------------------------ parse
// A malformed document or a value that does not match the target type. JSON.parse turns it into a
// runtime error; e7:json tryParse into null (+ lastError). Every other failure (out of memory,
// budget, a bad descriptor in the bytecode) is a VM error and is never caught here.
struct JsonFail {
  bool syntax;
  std::string msg;  // without the "JSON.parse: " prefix
};

struct JNode {
  enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
  bool b = false;
  double num = 0;
  std::u16string str;
  std::vector<JNode> arr;
  std::vector<std::pair<std::u16string, JNode>> obj;
};

struct JParser {
  VM& vm;
  const char16_t* s;
  size_t n, i = 0;
  int depth = 0;
  size_t tempBytes = 0, checkedAt = 0;  // transient C++ memory held by the tree (memory limit)
  [[noreturn]] void fail(const char* what) {
    throw JsonFail{true, std::string(what) + " at position " + std::to_string(i)};
  }
  void account(size_t bytes) {
    tempBytes += bytes;
    if (tempBytes - checkedAt >= (64u << 10)) {
      checkedAt = tempBytes;
      vm.checkTemp(tempBytes);
    }
  }
  void ws() {
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
  }
  JNode parse() {
    ws();
    JNode v = value();
    ws();
    if (i != n) fail("unexpected data after JSON value");
    return v;
  }
  bool lit(const char* w) {
    size_t l = strlen(w);
    if (i + l > n) return false;
    for (size_t k = 0; k < l; k++)
      if (s[i + k] != (char16_t)w[k]) return false;
    i += l;
    return true;
  }
  int hex(char16_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  std::u16string string() {
    std::u16string out;
    i++;  // opening quote
    while (true) {
      if (i >= n) fail("unterminated string");
      char16_t c = s[i++];
      if (c == '"') break;
      if (c < 0x20) fail("bad control character in string");
      if (c != '\\') { out += c; continue; }
      if (i >= n) fail("bad escape");
      char16_t e = s[i++];
      switch (e) {
        case '"': out += u'"'; break;
        case '\\': out += u'\\'; break;
        case '/': out += u'/'; break;
        case 'b': out += u'\b'; break;
        case 'f': out += u'\f'; break;
        case 'n': out += u'\n'; break;
        case 'r': out += u'\r'; break;
        case 't': out += u'\t'; break;
        case 'u': {
          if (i + 4 > n) fail("bad unicode escape");
          int v = 0;
          for (int k = 0; k < 4; k++) {
            int h = hex(s[i + k]);
            if (h < 0) fail("bad unicode escape");
            v = v * 16 + h;
          }
          i += 4;
          out += (char16_t)v;  // lone surrogates are kept, as in JavaScript
          break;
        }
        default: fail("bad escape");
      }
    }
    return out;
  }
  JNode value() {
    // an e7 limit, not a syntax error (JavaScript has none): not catchable, tryParse still returns null
    if (++depth > 256) throw JsonFail{false, "nesting deeper than 256 levels (an e7 limit) at position " + std::to_string(i)};
    account(sizeof(JNode) + 16);
    JNode v;
    if (i >= n) fail("unexpected end of input");
    char16_t c = s[i];
    if (c == '{') {
      v.t = JNode::Obj;
      i++;
      ws();
      if (i < n && s[i] == '}') { i++; depth--; return v; }
      while (true) {
        ws();
        if (i >= n || s[i] != '"') fail("expected property name");
        std::u16string k = string();
        account(k.size() * sizeof(char16_t));
        ws();
        if (i >= n || s[i] != ':') fail("expected ':'");
        i++;
        ws();
        JNode x = value();
        bool replaced = false;
        for (auto& kv : v.obj)
          if (kv.first == k) { kv.second = std::move(x); replaced = true; break; }
        if (!replaced) v.obj.emplace_back(std::move(k), std::move(x));
        ws();
        if (i < n && s[i] == ',') { i++; continue; }
        if (i < n && s[i] == '}') { i++; break; }
        fail("expected ',' or '}'");
      }
    } else if (c == '[') {
      v.t = JNode::Arr;
      i++;
      ws();
      if (i < n && s[i] == ']') { i++; depth--; return v; }
      while (true) {
        ws();
        v.arr.push_back(value());
        ws();
        if (i < n && s[i] == ',') { i++; continue; }
        if (i < n && s[i] == ']') { i++; break; }
        fail("expected ',' or ']'");
      }
    } else if (c == '"') {
      v.t = JNode::Str;
      v.str = string();
      account(v.str.size() * sizeof(char16_t));
    } else if (lit("true")) { v.t = JNode::Bool; v.b = true; }
    else if (lit("false")) { v.t = JNode::Bool; v.b = false; }
    else if (lit("null")) { v.t = JNode::Null; }
    else if (c == '-' || (c >= '0' && c <= '9')) {
      size_t st = i;
      if (s[i] == '-') i++;
      if (i >= n) fail("bad number");
      if (s[i] == '0') i++;
      else if (s[i] >= '1' && s[i] <= '9') while (i < n && s[i] >= '0' && s[i] <= '9') i++;
      else fail("bad number");
      if (i < n && s[i] == '.') {
        i++;
        if (i >= n || s[i] < '0' || s[i] > '9') fail("bad number");
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
      }
      if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        if (i >= n || s[i] < '0' || s[i] > '9') fail("bad number");
        while (i < n && s[i] >= '0' && s[i] <= '9') i++;
      }
      std::string num;
      for (size_t k = st; k < i; k++) num.push_back((char)s[k]);
      v.t = JNode::Num;
      v.num = parseDoubleC(num);
    } else {
      fail("unexpected token");
    }
    depth--;
    return v;
  }
};

struct Builder {
  VM& vm;
  ModuleRT* mod;
  const std::string& d;
  // the object descriptors being built, innermost last: "@<n>" (a recursive type, D6) refers to the n-th
  std::vector<size_t> recs = {};
  size_t openObjs = 0;  // validate(): object descriptors open around the current position
  [[noreturn]] void fail(const std::string& path, const char* what) {
    throw JsonFail{false, std::string(what) + " at " + (path.empty() ? std::string("$") : path)};
  }
  // The descriptor comes from bytecode, so it is validated once up front (grammar, bounds, depth);
  // build() and skip() rely on it being well-formed.
  [[noreturn]] void bad() { vm.error("bad JSON descriptor"); }
  size_t number(size_t p, char term) {  // 1..9 digits followed by `term`; returns index after `term`
    size_t q = p;
    while (q < d.size() && d[q] >= '0' && d[q] <= '9' && q - p < 9) q++;
    if (q == p || q >= d.size() || d[q] != term) bad();
    return q + 1;
  }
  size_t validate(size_t p, int depth) {
    if (depth > 300 || p >= d.size()) bad();
    switch (d[p]) {
      case 'n': case 'b': case 's': return p + 1;
      case 'i': {
        if (p + 1 >= d.size()) bad();
        int vt = d[p + 1] - '0';
        if (vt < VT_I8 || vt > VT_U64) bad();
        return p + 2;
      }
      case '?': {
        size_t q = p + 1;
        if (q < d.size() && d[q] == '~') q++;
        if (q >= d.size() || d[q] == '?' || d[q] == '~') bad();
        return validate(q, depth + 1);
      }
      case '~':
        if (p + 1 >= d.size() || d[p + 1] == '?' || d[p + 1] == '~') bad();
        return validate(p + 1, depth + 1);
      case '[': case 'M': return validate(p + 1, depth + 1);
      case '@': {
        // a back-reference to the n-th enclosing object descriptor (a recursive type)
        size_t q = p + 1;
        while (q < d.size() && d[q] >= '0' && d[q] <= '9' && q - p < 6) q++;
        size_t n = (size_t)atoi(d.c_str() + p + 1);
        if (q == p + 1 || n == 0 || n > openObjs) bad();
        return q;
      }
      case '{': case 't': {
        struct Open {
          size_t& n;
          bool on;
          ~Open() { if (on) n--; }
        } open{openObjs, d[p] == '{'};
        if (open.on) openObjs++;
        size_t q = number(p + 1, ':');
        size_t cnt = (size_t)atoi(d.c_str() + q);
        q = number(q, ':');
        if (cnt > d.size()) bad();
        for (size_t k = 0; k < cnt; k++) {
          if (d[p] == '{') {
            while (q < d.size() && d[q] != '=') {
              if (d[q] == '\\') q++;
              q++;
            }
            if (q >= d.size()) bad();
            q++;  // '='
          }
          q = validate(q, depth + 1);
          if (q >= d.size() || d[q] != ';') bad();
          q++;
        }
        return q;
      }
      default: bad();
    }
  }
  // skip one descriptor starting at p, return end position
  size_t skip(size_t p) {
    if (p >= d.size()) vm.error("bad JSON descriptor");
    char c = d[p];
    switch (c) {
      case 'n': case 'b': case 's': return p + 1;
      case 'i': return p + 2;
      case '?': case '~': case '[': case 'M': return skip(p + 1);
      case '@': {
        size_t q = p + 1;
        while (q < d.size() && d[q] >= '0' && d[q] <= '9') q++;
        if (q == p + 1) vm.error("bad JSON descriptor");
        return q;
      }
      case '{': {
        size_t q = d.find(':', p);
        q = d.find(':', q + 1);
        size_t cnt = (size_t)atoi(d.c_str() + d.find(':', p) + 1);
        q++;
        for (size_t k = 0; k < cnt; k++) {
          while (q < d.size() && d[q] != '=') { if (d[q] == '\\') q++; q++; }
          q = skip(q + 1);
          if (q >= d.size() || d[q] != ';') vm.error("bad JSON descriptor");
          q++;
        }
        return q;
      }
      case 't': {
        size_t c1 = d.find(':', p);
        size_t c2 = d.find(':', c1 + 1);
        size_t cnt = (size_t)atoi(d.c_str() + c1 + 1);
        size_t q = c2 + 1;
        for (size_t k = 0; k < cnt; k++) {
          q = skip(q);
          if (q >= d.size() || d[q] != ';') vm.error("bad JSON descriptor");
          q++;
        }
        return q;
      }
      default: vm.error("bad JSON descriptor");
    }
  }
  uint8_t vtOfDesc(size_t p) {
    char c = d[p];
    if (c == 'n') return VT_NUMBER;
    if (c == 'b') return VT_BOOL;
    if (c == 'i') return (uint8_t)(d[p + 1] - '0');
    return VT_REF;
  }
  Slot build(const JNode* v, size_t p, const std::string& path) {
    char c = d[p];
    Slot out;
    out.u = 0;
    switch (c) {
      case '?': {
        if (v && v->t == JNode::Null) { out.r = nullptr; return out; }
        Slot inner = build(v, p + 1, path);
        if (d[p + 1] == '~') return inner;  // already boxed (or undefined) by the '~' layer
        return boxValue(vm, inner, vtOfDesc(p + 1));
      }
      case '~': {
        if (!v) { out.r = vm.undef(); return out; }
        Slot inner = build(v, p + 1, path);
        return boxValue(vm, inner, vtOfDesc(p + 1));
      }
      default: break;
    }
    if (c == '@') {
      // the enclosing object descriptor n levels out
      size_t n = (size_t)atoi(d.c_str() + p + 1);
      if (n == 0 || n > recs.size()) vm.error("bad JSON descriptor back-reference");
      return build(v, recs[recs.size() - n], path);
    }
    if (!v) fail(path, "missing property");
    switch (c) {
      case 'n':
        if (v->t != JNode::Num) fail(path, "expected a number");
        out.f = v->num;
        return out;
      case 'b':
        if (v->t != JNode::Bool) fail(path, "expected a boolean");
        out.i = v->b;
        return out;
      case 's':
        if (v->t != JNode::Str) fail(path, "expected a string");
        out.r = vm.newString(v->str);
        return out;
      case 'i': {
        if (v->t != JNode::Num || std::trunc(v->num) != v->num) fail(path, "expected an integer");
        uint8_t vt = (uint8_t)(d[p + 1] - '0');
        double x = v->num;
        static const double lo[] = {0, 0, -128, 0, -32768, 0, -2147483648.0, 0, -9223372036854775808.0, 0};
        static const double hi[] = {0, 0, 127, 255, 32767, 65535, 2147483647.0, 4294967295.0, 9223372036854775807.0, 18446744073709551615.0};
        // 2^63 - 1 and 2^64 - 1 are not representable as doubles (they round up to 2^63 / 2^64), so the
        // 64-bit types use exclusive upper bounds: x < 2^63 (i64) and x < 2^64 (u64).
        bool inRange = vt >= VT_I8 && vt <= VT_U64 &&
                       (vt == VT_I64 ? (x >= -9223372036854775808.0 && x < 9223372036854775808.0)
                        : vt == VT_U64 ? (x >= 0 && x < 18446744073709551616.0)
                                       : (x >= lo[vt] && x <= hi[vt]));
        if (!inRange) fail(path, "integer out of range");
        out.i = doubleToInt64Bits(x);
        return out;
      }
      case '[': {
        if (v->t != JNode::Arr) fail(path, "expected an array");
        uint8_t evt = vtOfDesc(p + 1);
        if (d[p + 1] == '?' || d[p + 1] == '~') evt = VT_REF;
        ArrObj* arr = vm.newArray(evt, (uint32_t)v->arr.size());
        for (size_t k = 0; k < v->arr.size(); k++) arr->data[arr->len++] = build(&v->arr[k], p + 1, path + "[" + std::to_string(k) + "]");
        out.r = arr;
        return out;
      }
      case 'M': {
        if (v->t != JNode::Obj) fail(path, "expected an object");
        uint8_t vvt = (d[p + 1] == '?' || d[p + 1] == '~') ? (uint8_t)VT_REF : vtOfDesc(p + 1);
        MapObj* m = vm.newMap(VT_REF, vvt, KM_STR, false);
        for (auto& kv : v->obj) {
          Slot k;
          k.r = vm.newString(kv.first);
          Slot val = build(&kv.second, p + 1, path + "." + utf16ToUtf8(kv.first));
          // reuse native map insertion via Map.set semantics
          jsonMapSet(vm, m, k, val);
        }
        out.r = m;
        return out;
      }
      case '{': {
        if (v->t != JNode::Obj) fail(path, "expected an object");
        size_t c1 = d.find(':', p), c2 = d.find(':', c1 + 1);
        size_t cls = (size_t)atoi(d.c_str() + p + 1);
        size_t cnt = (size_t)atoi(d.c_str() + c1 + 1);
        if (!mod || cls >= mod->classes.size()) vm.error("bad JSON descriptor class");
        ClassRT* c = &mod->classes[cls];
        if (c->nfields != cnt) vm.error("bad JSON descriptor class shape");
        InstObj* in = vm.newInstance(c);
        recs.push_back(p);
        struct Pop {
          std::vector<size_t>& r;
          ~Pop() { r.pop_back(); }
        } pop{recs};
        size_t q = c2 + 1;
        for (size_t k = 0; k < cnt; k++) {
          std::string name;
          while (q < d.size() && d[q] != '=') {
            if (d[q] == '\\') q++;
            name.push_back(d[q++]);
          }
          q++;
          std::u16string uname = utf8ToUtf16(name);
          const JNode* fv = nullptr;
          for (auto& kv : v->obj)
            if (kv.first == uname) { fv = &kv.second; break; }
          auto it = c->fieldIndex.find(name);
          if (it == c->fieldIndex.end()) vm.error("bad JSON descriptor field");
          uint8_t want = (d[q] == '?' || d[q] == '~') ? (uint8_t)VT_REF : vtOfDesc(q);
          if (c->fieldVT[it->second] != want) vm.error("bad JSON descriptor field type");
          in->fields()[it->second] = build(fv, q, path + "." + name);
          if (!fv && c->presence) setFieldPresent(in, it->second, false);  // a missing optional property (D7)
          q = skip(q);
          q++;  // ';'
        }
        out.r = in;
        return out;
      }
      case 't': {
        if (v->t != JNode::Arr) fail(path, "expected a tuple array");
        size_t c1 = d.find(':', p), c2 = d.find(':', c1 + 1);
        size_t cls = (size_t)atoi(d.c_str() + p + 1);
        size_t cnt = (size_t)atoi(d.c_str() + c1 + 1);
        if (!mod || cls >= mod->classes.size()) vm.error("bad JSON descriptor class");
        ClassRT* c = &mod->classes[cls];
        if (c->nfields != cnt) vm.error("bad JSON descriptor class shape");
        if (v->arr.size() != cnt) fail(path, "tuple has the wrong number of elements");
        InstObj* in = vm.newInstance(c);
        size_t q = c2 + 1;
        for (size_t k = 0; k < cnt; k++) {
          uint8_t want = (d[q] == '?' || d[q] == '~') ? (uint8_t)VT_REF : vtOfDesc(q);
          if (c->fieldVT[k] != want) vm.error("bad JSON descriptor field type");
          in->fields()[k] = build(&v->arr[k], q, path + "[" + std::to_string(k) + "]");
          q = skip(q) + 1;
        }
        out.r = in;
        return out;
      }
      default:
        vm.error("bad JSON descriptor");
    }
  }
};

// Parses `text` against the descriptor; throws JsonFail for a malformed or mismatching document.
static Slot parseTyped(VM& vm, StrObj* text, const std::string& desc) {
  Builder b{vm, currentModule(vm), desc};
  if (b.validate(0, 0) != desc.size()) vm.error("bad JSON descriptor");
  JParser p{vm, text->chars(), text->len};
  JNode root = p.parse();
  Slot v = b.build(&root, 0, "$");
  return boxValue(vm, v, b.vtOfDesc(0) != VT_REF && desc[0] != '?' && desc[0] != '~' ? b.vtOfDesc(0) : (uint8_t)VT_REF);
}

E7_NATIVE(j_parse) {
  E7_UNUSED;
  StrObj* text = S(vm, a[0]);
  std::string desc = strUtf8(S(vm, a[1]));
  std::string msg;
  try {
    r->r = parseTyped(vm, text, desc).r;
    return;
  } catch (JsonFail& f) {
    msg = (f.syntax ? "SyntaxError: JSON.parse: " : "JSON.parse: ") + f.msg;
  }
  vm.error("%s", msg.c_str());
}

// e7:json tryParse: like JSON.parse, but a malformed or mismatching document gives null and sets
// lastError(). Budget, memory and descriptor errors still stop the script.
E7_NATIVE(j_tryParse) {
  E7_UNUSED;
  StrObj* text = S(vm, a[0]);
  std::string desc = strUtf8(S(vm, a[1]));
  try {
    r->r = parseTyped(vm, text, desc).r;
    vm.jsonLastError.clear();
  } catch (JsonFail& f) {
    vm.jsonLastError = (f.syntax ? "SyntaxError: " : "") + f.msg;
    r->r = nullptr;
  }
}

E7_NATIVE(j_lastError) {
  E7_UNUSED;
  r->r = vm.newString(utf8ToUtf16(vm.jsonLastError));
}

void registerJsonNatives(VM& vm) {
  vm.registerNative("JSON.stringify", "R>R", j_stringify);
  vm.registerNative("JSON.stringifyIndent", "RF>R", j_stringifyIndent);
  vm.registerNative("JSON.stringifyIndentStr", "RR>R", j_stringifyIndentStr);
  vm.registerNative("JSON.parse", "RR>R", j_parse);
  vm.registerNative("JSON.tryParse", "RR>R", j_tryParse);
  // the same with descriptors of recursive types ("@<n>" back-references, e7vm 0.3)
  vm.registerNative("JSON.parseRec", "RR>R", j_parse);
  vm.registerNative("JSON.tryParseRec", "RR>R", j_tryParse);
  vm.registerNative("JSON.lastError", ">R", j_lastError);
}

}  // namespace e7
