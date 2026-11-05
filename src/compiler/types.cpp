#include "types.h"

#include <cstdio>

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

Type* Types::array(Type* e) {
  std::string k = "A" + ptrKey(e);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Array);
  t->elem = e;
  return interned_[k] = t;
}
Type* Types::set(Type* e) {
  std::string k = "S" + ptrKey(e);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Set);
  t->elem = e;
  return interned_[k] = t;
}
Type* Types::map(Type* kk, Type* v) {
  std::string k = "M" + ptrKey(kk) + "," + ptrKey(v);
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Map);
  t->key = kk;
  t->val = v;
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
Type* Types::tuple(const std::vector<Type*>& elems) {
  std::string k = "T";
  for (Type* e : elems) k += ptrKey(e) + ",";
  auto it = interned_.find(k);
  if (it != interned_.end()) return it->second;
  Type* t = mk(TT::Tuple);
  t->elems = elems;
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
  for (auto& p : props) k += p.name + ":" + ptrKey(p.type) + (p.optional ? "?" : "") + ";";
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

std::string typeName(const Type* t) {
  if (!t) return "?";
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
      std::string e = typeName(t->elem);
      if (t->elem->t == TT::Optional || t->elem->t == TT::Func) e = "(" + e + ")";
      return e + "[]";
    }
    case TT::Bytes: return "Uint8Array";
    case TT::ArrayBuffer: return "ArrayBuffer";
    case TT::DataView: return "DataView";
    case TT::Map: return "Map<" + typeName(t->key) + ", " + typeName(t->val) + ">";
    case TT::Set: return "Set<" + typeName(t->elem) + ">";
    case TT::Func: {
      std::string s = "(";
      for (size_t i = 0; i < t->params.size(); i++) {
        if (i) s += ", ";
        if (t->restParam && i + 1 == t->params.size()) s += "...";
        s += "p" + std::to_string(i) + (t->optParams[i] ? "?: " : ": ") + typeName(t->params[i]);
      }
      return s + ") => " + typeName(t->ret);
    }
    case TT::Class: return t->cls->name;
    case TT::Object: {
      if (!t->shape->name.empty()) return t->shape->name;
      std::string s = "{ ";
      for (auto& p : t->shape->props) s += p.name + (p.optional ? "?: " : ": ") + typeName(p.type) + "; ";
      return s + "}";
    }
    case TT::Tuple: {
      std::string s = "[";
      for (size_t i = 0; i < t->elems.size(); i++) s += (i ? ", " : "") + typeName(t->elems[i]);
      return s + "]";
    }
    case TT::Optional: {
      std::string s = typeName(t->elem);
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
  }
  return "?";
}

}  // namespace e7
