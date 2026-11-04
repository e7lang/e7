// JSON.stringify (JavaScript semantics) and typed JSON.parse (validating, descriptor-driven).
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

  void nl(const std::u16string& indent) {
    if (gap.empty()) return;
    out += u'\n';
    out += indent;
  }
  // returns false if the value is "undefined" (omitted)
  bool value(Slot v, uint8_t vt, const std::u16string& indent) {
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
    if (stack.count(o)) vm.error("TypeError: Converting circular structure to JSON");
    if (++depth > 256) vm.error("RangeError: JSON.stringify: nesting deeper than 256 levels");
    stack.insert(o);
    std::u16string inner = indent + gap;
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
      for (uint32_t i = 0; i < in->cls->nfields; i++) {
        Slot fv = in->fields()[i];
        uint8_t fvt = in->cls->fieldVT[i];
        if (fvt == VT_REF && (fv.r == vm.undef() || (fv.r && fv.r->type == OT::Closure))) continue;
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
      }
      if (b->len) nl(indent);
      out += u'}';
    } else {
      out += u"{}";
    }
    stack.erase(o);
    depth--;
    if (out.size() > 0x3FFFFFFF) vm.error("RangeError: invalid string length");
    return true;
  }
};

static void stringifyImpl(VM& vm, Slot v, const std::u16string& gap, Slot* r) {
  Stringifier s{vm, gap, {}, {}, 0};
  if (!s.value(v, VT_REF, u"")) { r->r = vm.newStringAscii("undefined"); return; }
  r->r = vm.newString(s.out);
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
  [[noreturn]] void fail(const char* what) {
    vm.error("SyntaxError: JSON.parse: %s at position %zu", what, i);
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
          out += (char16_t)v;
          break;
        }
        default: fail("bad escape");
      }
    }
    return out;
  }
  JNode value() {
    if (++depth > 256) fail("nesting deeper than 256 levels");
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
  [[noreturn]] void fail(const std::string& path, const char* what) {
    vm.error("JSON.parse: %s at %s", what, path.empty() ? "$" : path.c_str());
  }
  // skip one descriptor starting at p, return end position
  size_t skip(size_t p) {
    if (p >= d.size()) vm.error("bad JSON descriptor");
    char c = d[p];
    switch (c) {
      case 'n': case 'b': case 's': return p + 1;
      case 'i': return p + 2;
      case '?': case '~': case '[': case 'M': return skip(p + 1);
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
        return boxValue(vm, inner, vtOfDesc(p + 1 + (d[p + 1] == '~' ? 1 : 0)));
      }
      case '~': {
        if (!v) { out.r = vm.undef(); return out; }
        Slot inner = build(v, p + 1, path);
        return boxValue(vm, inner, vtOfDesc(p + 1));
      }
      default: break;
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
        if (vt < VT_I8 || vt > VT_U64 || x < lo[vt] || x > hi[vt]) fail(path, "integer out of range");
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

E7_NATIVE(j_parse) {
  E7_UNUSED;
  StrObj* text = S(vm, a[0]);
  std::string desc = strUtf8(S(vm, a[1]));
  JParser p{vm, text->chars(), text->len};
  JNode root = p.parse();
  Builder b{vm, currentModule(vm), desc};
  Slot v = b.build(&root, 0, "$");
  r->r = boxValue(vm, v, b.vtOfDesc(0) != VT_REF && desc[0] != '?' && desc[0] != '~' ? b.vtOfDesc(0) : (uint8_t)VT_REF).r;
}

void registerJsonNatives(VM& vm) {
  vm.registerNative("JSON.stringify", "R>R", j_stringify);
  vm.registerNative("JSON.stringifyIndent", "RF>R", j_stringifyIndent);
  vm.registerNative("JSON.stringifyIndentStr", "RR>R", j_stringifyIndentStr);
  vm.registerNative("JSON.parse", "RR>R", j_parse);
}

}  // namespace e7
