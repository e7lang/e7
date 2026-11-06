#include "types.h"

#include <algorithm>
#include <cstdio>

#include "../core/numfmt.h"

namespace e7 {

Types::Types() {
  number_ = mk(TT::Number);
  bool_ = mk(TT::Bool);
  string_ = mk(TT::String);
  bigint_ = mk(TT::BigInt);
  void_ = mk(TT::Void);
  null_ = mk(TT::Null);
  undef_ = mk(TT::Undefined);
  never_ = mk(TT::Never);
  unknown_ = mk(TT::Unknown);
  bytes_ = mk(TT::Bytes);
  abuf_ = mk(TT::ArrayBuffer);
  dview_ = mk(TT::DataView);
  handle_ = mk(TT::Handle);
  tenc_ = mk(TT::TextEncoder);
  tdec_ = mk(TT::TextDecoder);
  error_ = mk(TT::Error);
  caught_ = mk(TT::Caught);
  date_ = mk(TT::Date);
  for (uint8_t v = VT_I8; v <= VT_U64; v++) {
    ints_[v] = mk(TT::Int);
    ints_[v]->vt = v;
  }
}

Type* Types::mk(TT t) {
  all_.emplace_back(new Type());
  all_.back()->t = t;
  return all_.back().get();
}

static std::string ptrKey(const void* p) {
  char b[32];
  snprintf(b, sizeof b, "%p", p);
  return b;
}

Type* Types::array(Type* e, bool ro) {
  std::string k = (ro ? "a" : "A") + ptrKey(e);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Array);
  t->elem = e;
  t->ro = ro;
  return interned_[k] = t;
}
Type* Types::set(Type* e, bool ro) {
  std::string k = (ro ? "s" : "S") + ptrKey(e);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Set);
  t->elem = e;
  t->ro = ro;
  return interned_[k] = t;
}
Type* Types::map(Type* kk, Type* v, bool ro) {
  std::string k = (ro ? "m" : "M") + ptrKey(kk) + "," + ptrKey(v);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Map);
  t->key = kk;
  t->val = v;
  t->ro = ro;
  return interned_[k] = t;
}
Type* Types::func(const std::vector<Type*>& params, Type* ret, const std::vector<bool>& opt, bool rest) {
  std::string k = "F";
  for (size_t i = 0; i < params.size(); i++) {
    k += ptrKey(params[i]);
    k += (i < opt.size() && opt[i]) ? "?" : ",";
  }
  k += rest ? "..." : "";
  k += ">" + ptrKey(ret);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Func);
  t->params = params;
  t->optParams = opt;
  t->optParams.resize(params.size(), false);
  t->restParam = rest;
  t->ret = ret;
  return interned_[k] = t;
}
Type* Types::tuple(const std::vector<Type*>& elems, bool ro) {
  std::string k = ro ? "t" : "T";
  for (Type* e : elems) k += ptrKey(e) + ",";
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Tuple);
  t->elems = elems;
  t->ro = ro;
  return interned_[k] = t;
}
Type* Types::optional(Type* base, bool null, bool undef) {
  if (base->t == TT::Optional) {
    null = null || base->hasNull;
    undef = undef || base->hasUndef;
    base = base->elem;
  }
  if (base->t == TT::Null) { null = true; base = nullptr; }
  else if (base->t == TT::Undefined) { undef = true; base = nullptr; }
  if (!base) return null && !undef ? null_ : undef && !null ? undef_ : null_;
  if (!null && !undef) return base;
  std::string k = std::string("O") + ptrKey(base) + (null ? "n" : "") + (undef ? "u" : "");
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Optional);
  t->elem = base;
  t->hasNull = null;
  t->hasUndef = undef;
  return interned_[k] = t;
}
Type* Types::classType(ClassSym* c) {
  std::string k = "C" + ptrKey(c);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Class);
  t->cls = c;
  return interned_[k] = t;
}
Type* Types::objectType(ObjShape* s) {
  std::string k = "N" + ptrKey(s);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Object);
  t->shape = s;
  return interned_[k] = t;
}
Type* Types::anonObject(const std::vector<ObjShape::Prop>& props) {
  std::string k = "{";
  for (auto& p : props) k += p.name + ":" + ptrKey(p.type) + (p.optional ? "?" : "") + (p.undefOk ? "!" : "") + (p.readonly ? "r" : "") + ";";
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  shapes.emplace_back(new ObjShape());
  ObjShape* s = shapes.back().get();
  s->props = props;
  Type* t = mk(TT::Object);
  t->shape = s;
  return interned_[k] = t;
}
Type* Types::enumType(EnumSym* e) {
  std::string k = "E" + ptrKey(e);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Enum);
  t->en = e;
  return interned_[k] = t;
}

Type* Types::generator(Type* y, Type* r, Type* n, uint8_t gk) {
  std::string k = "G" + std::to_string(gk) + ptrKey(y) + "," + ptrKey(r) + "," + ptrKey(n);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Generator);
  t->elem = y;
  t->ret = r;
  t->val = n;
  t->gk = gk;
  return interned_[k] = t;
}
Type* Types::asyncGenerator(Type* y, Type* r, Type* n, uint8_t gk) {
  std::string k = "AG" + std::to_string(gk) + ptrKey(y) + "," + ptrKey(r) + "," + ptrKey(n);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::AsyncGenerator);
  t->elem = y;
  t->ret = r;
  t->val = n;
  t->gk = gk;
  return interned_[k] = t;
}
Type* Types::typedArray(uint8_t kind) {
  std::string k = "TA" + std::to_string(kind);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::TypedArray);
  t->vt = kind;
  return interned_[k] = t;
}
Type* Types::regexp(int32_t caps, const std::vector<std::string>& names) {
  std::string k = "RE" + std::to_string(caps);
  for (auto& n : names) k += "," + n;
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::RegExp);
  t->reCaps = caps;
  t->reNames = names;
  return interned_[k] = t;
}
Type* Types::matchArray(Type* re, bool exec) {
  std::string k = std::string(exec ? "MX" : "MM") + ptrKey(re);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Array);
  t->elem = string_;
  t->re = re;
  t->reExec = exec;
  return interned_[k] = t;
}
Type* Types::promise(Type* e) {
  std::string k = "P" + ptrKey(e);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Promise);
  t->elem = e;
  return interned_[k] = t;
}

Type* Types::strLit(std::vector<std::u16string> vals) {
  std::sort(vals.begin(), vals.end());
  vals.erase(std::unique(vals.begin(), vals.end()), vals.end());
  std::string k = "LS";
  for (auto& v : vals) {
    k += std::to_string(v.size()) + ":";
    for (char16_t c : v) { k += (char)(c & 0xFF); k += (char)(c >> 8); }
  }
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::String);
  t->lit = true;
  t->litBase = string_;
  t->strLits = vals;
  return interned_[k] = t;
}
Type* Types::numLit(std::vector<double> vals) {
  for (double& d : vals) d = d + 0.0;  // -0 is the literal 0
  std::sort(vals.begin(), vals.end(), [](double a, double b) { return a < b || (a != a && b == b); });
  vals.erase(std::unique(vals.begin(), vals.end(), [](double a, double b) { return a == b || (a != a && b != b); }), vals.end());
  std::string k = "LN";
  for (double d : vals) {
    char b[40];
    snprintf(b, sizeof b, "%.17g,", d);
    k += b;
  }
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Number);
  t->lit = true;
  t->litBase = number_;
  t->numLits = vals;
  return interned_[k] = t;
}
Type* Types::boolLit(uint8_t mask) {
  if ((mask & 3) == 3) return bool_;
  std::string k = "LB" + std::to_string(mask & 3);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Bool);
  t->lit = true;
  t->litBase = bool_;
  t->boolLits = mask & 3;
  return interned_[k] = t;
}
Type* Types::litUnion(Type* a, Type* b) {
  if (!a->lit || !b->lit || a->litBase != b->litBase) return nullptr;
  if (a->t == TT::String) {
    std::vector<std::u16string> v = a->strLits;
    v.insert(v.end(), b->strLits.begin(), b->strLits.end());
    return strLit(v);
  }
  if (a->t == TT::Number) {
    std::vector<double> v = a->numLits;
    v.insert(v.end(), b->numLits.begin(), b->numLits.end());
    return numLit(v);
  }
  return boolLit(a->boolLits | b->boolLits);
}

Kind kindOf(const Type* t) {
  switch (t->t) {
    case TT::Number: return K_F;
    case TT::Bool: case TT::Int: return K_I;
    case TT::Void: case TT::Never: return K_V;
    case TT::Enum: return t->en && t->en->isString ? K_R : K_F;
    default: return K_R;
  }
}
uint8_t vtOf(const Type* t) {
  switch (t->t) {
    case TT::Number: return VT_NUMBER;
    case TT::Bool: return VT_BOOL;
    case TT::Int: return t->vt;
    case TT::Enum: return t->en && t->en->isString ? VT_REF : VT_NUMBER;
    default: return VT_REF;
  }
}

static const char* intName(uint8_t vt) {
  switch (vt) {
    case VT_I8: return "i8"; case VT_U8: return "u8"; case VT_I16: return "i16"; case VT_U16: return "u16";
    case VT_I32: return "i32"; case VT_U32: return "u32"; case VT_I64: return "i64"; case VT_U64: return "u64";
    default: return "int?";
  }
}

// bounded: generic instances can nest types whose full names would grow exponentially
static std::string typeNameB(const Type* t, int& budget) {
  if (!t) return "?";
  if (--budget < 0) return "...";
  if (t->lit) {
    std::string s;
    auto sep = [&]() { if (!s.empty()) s += " | "; };
    for (auto& v : t->strLits) {
      sep();
      s += "\"";
      for (char c : utf16ToUtf8(v)) s += (c == '"' || c == '\\') ? std::string("\\") + c : std::string(1, c);
      s += "\"";
    }
    for (double d : t->numLits) { sep(); s += numberToString(d); }
    if (t->boolLits & 1) { sep(); s += "false"; }
    if (t->boolLits & 2) { sep(); s += "true"; }
    return s.empty() ? "never" : s;
  }
  switch (t->t) {
    case TT::Number: return "number";
    case TT::Bool: return "boolean";
    case TT::String: return "string";
    case TT::BigInt: return "bigint";
    case TT::Void: return "void";
    case TT::Null: return "null";
    case TT::Undefined: return "undefined";
    case TT::Never: return "never";
    case TT::Unknown: case TT::Caught: return "unknown";
    case TT::Int: return intName(t->vt);
    case TT::Array: {
      if (t->re) return t->reExec ? "RegExpExecArray" : "RegExpMatchArray";
      std::string e = typeNameB(t->elem, budget);
      if (t->elem->t == TT::Optional || t->elem->t == TT::Func) e = "(" + e + ")";
      return (t->ro ? "readonly " : "") + e + "[]";
    }
    case TT::Bytes: return "Uint8Array";
    case TT::ArrayBuffer: return "ArrayBuffer";
    case TT::DataView: return "DataView";
    case TT::Map: return (t->ro ? "ReadonlyMap<" : "Map<") + typeNameB(t->key, budget) + ", " + typeNameB(t->val, budget) + ">";
    case TT::Set: return (t->ro ? "ReadonlySet<" : "Set<") + typeNameB(t->elem, budget) + ">";
    case TT::Func: {
      std::string s = "(";
      for (size_t i = 0; i < t->params.size(); i++) {
        if (i) s += ", ";
        if (t->restParam && i + 1 == t->params.size()) s += "...";
        s += "p" + std::to_string(i) + (t->optParams[i] ? "?: " : ": ") + typeNameB(t->params[i], budget);
      }
      return s + ") => " + typeNameB(t->ret, budget);
    }
    case TT::Class: return t->cls->name;
    case TT::Object: {
      if (!t->shape->name.empty()) return t->shape->name;
      std::string s = "{ ";
      for (auto& p : t->shape->props) s += p.name + (p.optional ? "?: " : ": ") + typeNameB(p.type, budget) + "; ";
      return s + "}";
    }
    case TT::Tuple: {
      std::string s = "[";
      for (size_t i = 0; i < t->elems.size(); i++) s += (i ? ", " : "") + typeNameB(t->elems[i], budget);
      return (t->ro ? "readonly " : "") + s + "]";
    }
    case TT::Optional: {
      std::string s = typeNameB(t->elem, budget);
      if (t->elem->t == TT::Func) s = "(" + s + ")";
      if (t->hasNull) s += " | null";
      if (t->hasUndef) s += " | undefined";
      return s;
    }
    case TT::Enum: return t->en->name;
    case TT::Handle: return "KeyHandle";
    case TT::TextEncoder: return "TextEncoder";
    case TT::TextDecoder: return "TextDecoder";
    case TT::Error: return "Error";
    case TT::Date: return "Date";
    case TT::RegExp: return "RegExp";
    case TT::Generator:
    case TT::AsyncGenerator: {
      static const char* const names[4] = {"Generator", "Iterator", "IterableIterator", "IteratorObject"};
      static const char* const anames[4] = {"AsyncGenerator", "AsyncIterator", "AsyncIterableIterator", "AsyncIterable"};
      std::string s = std::string(t->t == TT::AsyncGenerator ? anames[t->gk & 3] : names[t->gk & 3]) + "<" +
                      typeNameB(t->elem, budget);
      bool showRet = t->ret && t->ret->t != TT::Void, showNext = t->val && t->val->t != TT::Void;
      if (t->gk == 0 || showRet || showNext) s += ", " + (showRet || t->gk == 0 ? typeNameB(t->ret, budget) : std::string("void"));
      if (t->gk == 0 || showNext) s += ", " + (showNext ? typeNameB(t->val, budget) : std::string("unknown"));
      return s + ">";
    }
    case TT::Promise: return "Promise<" + typeNameB(t->elem, budget) + ">";
    case TT::TypedArray: {
      static const char* const n[11] = {"Uint8Array", "Int8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array",
                                        "Int32Array", "Uint32Array", "Float32Array", "Float64Array", "BigInt64Array",
                                        "BigUint64Array"};
      return t->vt < 11 ? n[t->vt] : "TypedArray";
    }
  }
  return "?";
}

std::string typeName(const Type* t) {
  int budget = 400;
  return typeNameB(t, budget);
}

}  // namespace e7
