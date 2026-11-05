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
  Handle, TextEncoder, TextDecoder, Error, Date, Unknown,
  Caught  // a catch variable: 'unknown' in TypeScript; narrow with instanceof Error
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

struct GenericSym;
struct ObjShape {
  std::string name;  // interface name or empty
  GenericSym* origin = nullptr;  // instance of a generic interface: the template and its type arguments
  std::vector<Type*> typeArgs;
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
enum class SymK { Var, Func, Class, Interface, Alias, Enum, Namespace, Builtin, TypeParam, Generic };

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
  bool isAbstract = false;   // abstract method: no body (a stub that is never called)
  int accessor = 0;          // 1 = getter "get x", 2 = setter "set x"
  // record method (object literal method or arrow property): calls the closure in field
  // `trampField` of the record (vtable entry so that interface method calls reach it)
  int trampField = -1;
  // instance of a generic function / method, or a method of a generic class instance: its name with
  // the type arguments (error messages say "in first<string>") and the instantiation depth
  std::string instName;
  int instDepth = 0;
  struct Scope* defScope = nullptr;  // lexical scope of the definition
};

struct ClassSym : Sym {
  ClassSym() : Sym(SymK::Class) {}
  Stmt* decl = nullptr;
  ClassSym* parent = nullptr;
  struct Field { std::string name; Type* type; Expr* init; bool optional; ClassSym* owner; Loc loc; bool readonly = false; };
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
  bool isAbstract = false;
  // generics: 1 = holder of the static members of a generic class (one per template),
  // 2 = an instance (Box<number>: the instance members, with the type parameters bound)
  int genericRole = 0;
  GenericSym* origin = nullptr;    // instance: the template
  std::vector<Type*> typeArgs;     // instance: the type arguments
  struct Scope* defScope = nullptr;  // instance: scope binding the type parameters
  std::string rtName;              // instance: the name at run time (console.log shows "Box")
  int instDepth = 0;
  std::unordered_map<std::string, GenericSym*> genericMethods;  // generic methods (not virtual)
  // abstract properties (declared here or inherited, not implemented yet): no slot in this class,
  // read and written by name; concrete subclasses declare them as fields
  std::map<std::string, Type*> abstractProps;
  // #private methods (non-virtual, called directly); private fields are ordinary fields named "#x"
  std::unordered_map<std::string, FuncSym*> privateMethods;
  std::string file;
  // accessor lookup helpers ("get x" / "set x" vtable slots; -1 when absent)
  int getterSlot(const std::string& n) const {
    auto it = methodIdx.find("get " + n);
    return it == methodIdx.end() ? -1 : it->second;
  }
  int setterSlot(const std::string& n) const {
    auto it = methodIdx.find("set " + n);
    return it == methodIdx.end() ? -1 : it->second;
  }
  Type* abstractProp(const std::string& n) const {
    auto it = abstractProps.find(n);
    return it == abstractProps.end() ? nullptr : it->second;
  }
};

struct InterfaceSym : Sym {
  InterfaceSym() : Sym(SymK::Interface) {}
  Stmt* decl = nullptr;
  Type* type = nullptr;
  int state = 0;
  struct Scope* defScope = nullptr;  // instance of a generic interface: scope binding the type parameters
  GenericSym* origin = nullptr;
  std::vector<Type*> typeArgs;
  bool inExtends = false;  // resolving the extends clause (a cycle there is a circular extends)
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

// A generic declaration (function, arrow function assigned to a const, class, interface, type alias,
// method): a template that is never checked itself. Each distinct list of type arguments gets an
// instance: a copy of the syntax tree checked with the type parameters bound to the concrete types.
struct GenericSym : Sym {
  GenericSym() : Sym(SymK::Generic) {}
  enum Kind { Func, Arrow, Class, Interface, Alias, Method } kind = Func;
  Stmt* decl = nullptr;            // Func / Class / Interface / Alias
  Expr* arrow = nullptr;           // Arrow
  ClassMember* member = nullptr;   // Method
  ClassSym* ownerClass = nullptr;  // Method: the class (instance) that declares it
  std::vector<TypeParam>* params = nullptr;
  struct Scope* defScope = nullptr;  // scope of the declaration
  FuncSym* parentFn = nullptr;       // enclosing function (null at top level)
  bool topLevel = true;
  bool isStatic = false;
  std::string file;
  ClassSym* holder = nullptr;      // Class: static members
  std::unordered_map<std::string, Sym*> instances;  // key: the type argument list
  std::vector<Sym*> order;                          // instances in creation order
  std::unordered_map<std::string, Type*> typeInstances;  // Alias
  std::unordered_map<std::string, bool> resolving;       // Alias: instances being resolved
  // the parameter list and return type of a function-like template (for inference)
  std::vector<Param>* fnParams() const;
  TypeNode* fnRet() const;
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
  Type* caught() { return caught_; }
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
      *handle_, *tenc_, *tdec_, *error_, *date_, *caught_;
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
// Own enumerable properties a value of a class / object type has (name, storage type): fields
// except #private ones, and abstract properties (declared by the subclass); object type properties.
inline std::vector<std::pair<std::string, Type*>> enumerableProps(Type* t) {
  std::vector<std::pair<std::string, Type*>> out;
  if (t->t == TT::Class) {
    for (auto& f : t->cls->fields)
      if (f.name.empty() || f.name[0] != '#') out.push_back({f.name, f.type});
    for (auto& ap : t->cls->abstractProps) out.push_back({ap.first, ap.second});
  } else if (t->t == TT::Object) {
    for (auto& p : t->shape->props) out.push_back({p.name, p.type});
  }
  return out;
}
// Optional primitives are stored boxed.
inline bool isBoxedOptional(const Type* t) {
  return t->t == TT::Optional && kindOf(t->elem) != K_R;
}

}  // namespace e7
