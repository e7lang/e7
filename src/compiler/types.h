// Semantic types and symbols for the E7 checker.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast.h"

namespace e7 {

enum class TT : uint8_t {
  Number, Bool, String, BigInt, Void, Null, Undefined, Never, Int,
  Array, Bytes, ArrayBuffer, DataView, Map, Set, Func, Class, Object, Tuple, Optional, Enum,
  Handle, TextEncoder, TextDecoder, Error, Date, Unknown
};

struct ObjShape;
struct EnumSym;

struct Type {
  TT t = TT::Unknown;
  uint8_t vt = VT_REF;  // for Int: VT_I8..VT_U64
  Type* elem = nullptr; // Array / Set / Optional base
  Type* key = nullptr;  // Map
  Type* val = nullptr;  // Map
  std::vector<Type*> params;  // Func
  std::vector<bool> optParams;
  bool restParam = false;     // last param is ...rest (Array type)
  Type* ret = nullptr;        // Func
  ClassSym* cls = nullptr;
  ObjShape* shape = nullptr;
  std::vector<Type*> elems;   // Tuple
  bool hasNull = false, hasUndef = false;  // Optional
  EnumSym* en = nullptr;
};

struct ObjShape {
  std::string name;  // interface name or empty
  struct Prop { std::string name; Type* type; bool optional; };
  std::vector<Prop> props;
  struct Method { std::string name; Type* fn; };
  std::vector<Method> methods;
  const Prop* findProp(const std::string& n) const {
    for (auto& p : props) if (p.name == n) return &p;
    return nullptr;
  }
  const Method* findMethod(const std::string& n) const {
    for (auto& m : methods) if (m.name == n) return &m;
    return nullptr;
  }
};

// ------------------------------------------------------------------ symbols
enum class SymK { Var, Func, Class, Interface, Alias, Enum, Namespace, Builtin, TypeParam };

struct Sym {
  SymK k;
  std::string name;
  Loc loc;
  explicit Sym(SymK kk) : k(kk) {}
  virtual ~Sym() = default;
};

struct VarSym : Sym {
  VarSym() : Sym(SymK::Var) {}
  Type* type = nullptr;
  bool isConst = false;
  bool isGlobal = false;
  int globalIdx = -1;
  FuncSym* owner = nullptr;  // declaring function (null for globals)
  bool captured = false;
  bool isParam = false;
  bool isThis = false;
  int paramIndex = -1;
  // lazily typed globals
  VarDecl* decl = nullptr;
  Stmt* declStmt = nullptr;
  bool typing = false;
  bool checked = false;
  // codegen
  int reg = -1;
  bool cellReady = false;
};

struct FuncSym : Sym {
  FuncSym() : Sym(SymK::Func) {}
  Type* type = nullptr;  // Func type
  std::vector<Param>* params = nullptr;
  TypeNode* retNode = nullptr;
  Stmt* body = nullptr;
  Expr* exprBody = nullptr;
  Stmt* declStmt = nullptr;  // function declaration statement
  Expr* arrow = nullptr;     // arrow / function expression
  FuncSym* parent = nullptr; // lexically enclosing function
  ClassSym* cls = nullptr;
  bool isMethod = false, isStatic = false, isCtor = false, isTopLevel = false, isInit = false;
  bool isNative = false;     // declare function
  bool isArrowThis = false;  // arrow: inherits 'this'
  bool exported = false;
  bool checked = false, checking = false;
  bool sigResolved = false;
  std::vector<VarSym*> paramSyms;
  VarSym* thisVar = nullptr;
  std::vector<VarSym*> captures;
  std::unordered_map<VarSym*, int> captureIndex;
  VarSym* selfVar = nullptr;   // nested function declaration: variable holding the closure
  std::vector<Type*> returnTypes;  // collected during inference
  std::string file;
  std::string qualName;
  int funcIdx = -1;
  bool inferRet = false;     // return type inferred from body
  bool hasDefaults = false;  // has default-valued params (hidden argc parameter)
  bool inheritCtor = false;  // synthesized constructor forwarding to the parent constructor
  struct Scope* defScope = nullptr;  // lexical scope of the definition
};

struct ClassSym : Sym {
  ClassSym() : Sym(SymK::Class) {}
  Stmt* decl = nullptr;
  ClassSym* parent = nullptr;
  struct Field { std::string name; Type* type; Expr* init; bool optional; ClassSym* owner; Loc loc; };
  std::vector<Field> fields;  // full layout, inherited first
  struct Method { std::string name; FuncSym* fn; };
  std::vector<Method> vtable;
  std::unordered_map<std::string, int> fieldIdx, methodIdx;
  FuncSym* ctor = nullptr;
  std::unordered_map<std::string, FuncSym*> staticMethods;
  std::unordered_map<std::string, VarSym*> staticFields;
  std::vector<Type*> implements;
  Type* type = nullptr;
  int classIdx = -1;
  int state = 0;  // 0 = unresolved, 1 = resolving, 2 = resolved
  bool isRecord = false;
  bool isTuple = false;
  std::string file;
};

struct InterfaceSym : Sym {
  InterfaceSym() : Sym(SymK::Interface) {}
  Stmt* decl = nullptr;
  Type* type = nullptr;
  int state = 0;
};

struct AliasSym : Sym {
  AliasSym() : Sym(SymK::Alias) {}
  Stmt* decl = nullptr;
  Type* type = nullptr;
  int state = 0;
};

struct EnumSym : Sym {
  EnumSym() : Sym(SymK::Enum) {}
  Stmt* decl = nullptr;
  bool isString = false;
  struct Member { std::string name; double num; std::u16string str; };
  std::vector<Member> members;
  Type* type = nullptr;
  int state = 0;
};

struct NamespaceSym : Sym {
  NamespaceSym() : Sym(SymK::Namespace) {}
  std::string module;  // builtin namespace ("Math", "e7:crypto") or file path
  std::unordered_map<std::string, Sym*> members;  // for file modules
  bool builtin = false;
};

struct BuiltinSym : Sym {
  BuiltinSym() : Sym(SymK::Builtin) {}
  std::string id;  // e.g. "parseInt", "u32"
};

// ------------------------------------------------------------------ type table
class Types {
 public:
  Types();
  Type* number() { return number_; }
  Type* boolean() { return bool_; }
  Type* string() { return string_; }
  Type* bigint() { return bigint_; }
  Type* voidT() { return void_; }
  Type* nullT() { return null_; }
  Type* undef() { return undef_; }
  Type* never() { return never_; }
  Type* unknown() { return unknown_; }
  Type* bytes() { return bytes_; }
  Type* arrayBuffer() { return abuf_; }
  Type* dataView() { return dview_; }
  Type* handle() { return handle_; }
  Type* textEncoder() { return tenc_; }
  Type* textDecoder() { return tdec_; }
  Type* error() { return error_; }
  Type* date() { return date_; }
  Type* intT(uint8_t vt) { return ints_[vt]; }
  Type* array(Type* e);
  Type* set(Type* e);
  Type* map(Type* k, Type* v);
  Type* func(const std::vector<Type*>& params, Type* ret, const std::vector<bool>& opt = {}, bool rest = false);
  Type* tuple(const std::vector<Type*>& elems);
  Type* optional(Type* base, bool null, bool undef);
  Type* classType(ClassSym* c);
  Type* objectType(ObjShape* s);  // nominal (interface) object type
  Type* anonObject(const std::vector<ObjShape::Prop>& props);  // structural, interned
  Type* enumType(EnumSym* e);
  std::vector<std::unique_ptr<ObjShape>> shapes;

 private:
  Type* mk(TT t);
  std::vector<std::unique_ptr<Type>> all_;
  Type *number_, *bool_, *string_, *bigint_, *void_, *null_, *undef_, *never_, *unknown_, *bytes_, *abuf_, *dview_,
      *handle_, *tenc_, *tdec_, *error_, *date_;
  Type* ints_[VT__COUNT] = {};
  std::map<std::string, Type*> interned_;
};

// helpers
std::string typeName(const Type* t);
inline bool isOptional(const Type* t) { return t->t == TT::Optional; }
inline bool isNumeric(const Type* t) { return t->t == TT::Number || t->t == TT::Int; }
inline bool isIntType(const Type* t) { return t->t == TT::Int; }
Kind kindOf(const Type* t);
uint8_t vtOf(const Type* t);
inline bool isSignedInt(const Type* t) {
  return t->t == TT::Int && (t->vt == VT_I8 || t->vt == VT_I16 || t->vt == VT_I32 || t->vt == VT_I64);
}
inline int intBits(const Type* t) {
  switch (t->vt) {
    case VT_I8: case VT_U8: return 8;
    case VT_I16: case VT_U16: return 16;
    case VT_I32: case VT_U32: return 32;
    default: return 64;
  }
}
// Base type of optional (or itself)
inline Type* baseOf(Type* t) { return t->t == TT::Optional ? t->elem : t; }
// Optional primitives are stored boxed.
inline bool isBoxedOptional(const Type* t) {
  return t->t == TT::Optional && kindOf(t->elem) != K_R;
}

}  // namespace e7
