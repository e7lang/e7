#include <algorithm>
#include <map>
#include "checker.h"
#include "intrinsics.h"
#include "suggest.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace e7 {

Checker::Checker(Program& p, std::function<SourceFile*(const std::string&, const std::string&)> resolve)
    : P(p), T(p.types), resolveModule(std::move(resolve)) {}

void Checker::err(Loc l, const std::string& msg) {
  for (auto& e : P.errors)
    if (e.file == curFile && e.loc.line == l.line && e.loc.col == l.col) return;  // one error per location
  // in the code of a generic instance, name it: "... (in first<string>)"
  P.errors.push_back(CompileError{curFile, l, instCtx.empty() ? msg : msg + " (in " + instCtx.back() + ")"});
}

// ------------------------------------------------------------------ scopes
Scope* Checker::pushScope(bool fnBoundary) {
  scopePool.emplace_back(new Scope());
  Scope* s = scopePool.back().get();
  s->parent = scope;
  s->fn = curFn;
  s->fnBoundary = fnBoundary;
  scope = s;
  return s;
}
void Checker::popScope() { scope = scope->parent; }

void Checker::declare(const std::string& name, Sym* s, Loc l) {
  Scope* saved = scope;
  while (scope->declareUp && scope->parent) scope = scope->parent;  // (one scope for all clauses of a switch)
  struct Restore { Scope*& sc; Scope* v; ~Restore() { sc = v; } } restore{scope, saved};
  if (scope->names.count(name)) {
    // only `export default` declares "default" (a reserved word)
    err(l, name == "default" ? std::string("a module cannot have more than one default export")
                             : "duplicate declaration of '" + name + "'");
    return;
  }
  s->loc = l;
  scope->names[name] = s;
  scope->pending.erase(name);
}

Sym* Checker::lookup(const std::string& name, bool* crossedFn) {
  bool crossed = false;
  for (Scope* s = scope; s; s = s->parent) {
    auto it = s->names.find(name);
    if (it != s->names.end()) {
      if (crossedFn) *crossedFn = crossed;
      return it->second;
    }
    // a let / const declared later in this block: the name means it (not an outer one), before its
    // declaration ran
    if (!s->pending.empty() && s->pending.count(name)) {
      pendingHit_ = name;
      return nullptr;
    }
    if (s->fnBoundary) crossed = true;
  }
  if (name.find('.') != std::string::npos) {
    if (crossedFn) *crossedFn = false;
    return lookupQualified(scope, name);
  }
  return nullptr;
}

// `ns.X` (a qualified type name, or an expression that resolveNamespaceRefs rewrote): export X of the module
// namespace ns (`import * as ns`, or one exported by `export * as ns`); null when ns is not one (a builtin
// module namespace or a shadowing local) or has no export X
Sym* Checker::lookupQualified(Scope* from, const std::string& name) {
  size_t dot = name.rfind('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 == name.size()) return nullptr;
  Sym* base = lookupFrom(from, name.substr(0, dot));
  if (!base || base->k != SymK::Namespace || static_cast<NamespaceSym*>(base)->builtin) return nullptr;
  auto& members = static_cast<NamespaceSym*>(base)->members;
  auto it = members.find(name.substr(dot + 1));
  return it == members.end() ? nullptr : it->second;
}

// Rewrites `ns.C` (C a class, enum, generic class or namespace exported by the module namespace ns) into the
// identifier "ns.C", which lookup() resolves to C: the code that handles a class, enum or namespace named by an
// identifier (`new C()`, `C.staticMember`, `x instanceof C`, `E.A`, `ns2.f()`) then applies unchanged. The
// original object stays in `a`: checkIdent turns the node back into a member access when, where it is
// checked, `ns` names something else (a local that shadows the import).
void Checker::resolveNamespaceRefs(FileInfo* fi) {
  bool any = false;
  for (auto& kv : fi->scope->names)
    if (kv.second->k == SymK::Namespace && !static_cast<NamespaceSym*>(kv.second)->builtin) any = true;
  if (!any) return;
  std::function<void(Expr*)> ex;
  std::function<void(Stmt*)> st;
  std::function<void(VarDecl&)> decl = [&](VarDecl& d) {
    ex(d.init);
    for (auto& el : d.elems) decl(el);
  };
  auto params = [&](std::vector<Param>& ps) {
    for (auto& p : ps) {
      ex(p.init);
      if (p.pattern) decl(*p.pattern);
    }
  };
  ex = [&](Expr* e) {
    if (!e) return;
    ex(e->a);
    ex(e->b);
    ex(e->c);
    ex(e->exprBody);
    for (Expr* x : e->list) ex(x);
    for (auto& p : e->props) ex(p.value);
    params(e->params);
    st(e->body);
    if (e->k != EK::Member || e->optional || !e->a || e->a->k != EK::Ident) return;
    Sym* base = lookupFrom(fi->scope, e->a->raw);
    if (!base || base->k != SymK::Namespace || static_cast<NamespaceSym*>(base)->builtin) return;
    auto& members = static_cast<NamespaceSym*>(base)->members;
    auto it = members.find(e->raw);
    if (it == members.end()) return;
    Sym* m = it->second;
    bool named = m->k == SymK::Class || m->k == SymK::Enum || m->k == SymK::Namespace ||
                 (m->k == SymK::Generic && static_cast<GenericSym*>(m)->kind == GenericSym::Class);
    if (!named) return;
    e->k = EK::Ident;
    e->raw = e->a->raw + "." + e->raw;
  };
  st = [&](Stmt* s) {
    if (!s) return;
    for (auto& d : s->decls) decl(d);
    params(s->params);
    st(s->body);
    st(s->then);
    st(s->els);
    st(s->init);
    for (Stmt* x : s->stmts) st(x);
    ex(s->expr);
    ex(s->update);
    for (auto& c : s->cases) {
      ex(c.test);
      for (Stmt* x : c.body) st(x);
    }
    for (auto& m : s->members) {
      ex(m.init);
      params(m.params);
      st(m.body);
    }
    for (auto& em : s->enumMembers) ex(em.init);
  };
  for (Stmt* s : fi->file->body) st(s);
}

std::string Checker::suggestName(const std::string& name, bool typeNames) {
  std::vector<std::string> cands;
  for (Scope* s = scope; s; s = s->parent)
    for (auto& kv : s->names) {
      SymK k = kv.second->k;
      bool isType = k == SymK::Class || k == SymK::Interface || k == SymK::Alias || k == SymK::Enum;
      bool isValue = k == SymK::Var || k == SymK::Func || k == SymK::Class || k == SymK::Enum ||
                     k == SymK::Namespace || k == SymK::Builtin;
      if (typeNames ? isType : isValue) cands.push_back(kv.first);
    }
  if (typeNames) {
    for (const char* n : {"number", "string", "boolean", "bigint", "void", "null", "undefined", "never", "i8", "u8",
                          "i16", "u16", "i32", "u32", "i64", "u64", "int", "Array", "ReadonlyArray", "Map",
                          "ReadonlyMap", "Set", "ReadonlySet", "Uint8Array", "ArrayBuffer", "DataView", "TextEncoder",
                          "TextDecoder", "Error", "Date"})
      cands.push_back(n);
  } else {
    cands.push_back("undefined");
    cands.push_back("null");
  }
  return didYouMean(name, cands);
}

std::string Checker::suggestMember(Type* t, const std::string& name) {
  std::vector<std::string> c;
  auto add = [&](std::initializer_list<const char*> l) {
    for (const char* n : l) c.push_back(n);
  };
  if (!t) return "";
  switch (t->t) {
    case TT::Class:
      for (auto& kv : t->cls->fieldIdx) c.push_back(kv.first);
      for (auto& kv : t->cls->methodIdx) c.push_back(kv.first);
      break;
    case TT::Object:
      for (auto& p : t->shape->props) c.push_back(p.name);
      for (auto& m : t->shape->methods) c.push_back(m.name);
      break;
    case TT::String:
      add({"at", "charAt", "charCodeAt", "codePointAt", "concat", "endsWith", "includes", "indexOf", "lastIndexOf",
           "length", "localeCompare", "normalize", "padEnd", "padStart", "repeat", "replace", "replaceAll", "slice",
           "split", "startsWith", "substring", "toLowerCase", "toString", "toUpperCase", "trim", "trimEnd",
           "trimStart"});
      break;
    case TT::Number: case TT::Enum:
      add({"toExponential", "toFixed", "toPrecision", "toString", "valueOf"});
      break;
    case TT::Int: case TT::Bool: case TT::BigInt: add({"toString"}); break;
    case TT::Array:
      add({"at", "concat", "copyWithin", "entries", "every", "fill", "filter", "find", "findIndex", "findLast",
           "findLastIndex", "flat", "flatMap", "forEach", "includes", "indexOf", "join", "keys", "lastIndexOf",
           "length", "map", "pop", "push", "reduce", "reduceRight", "reverse", "shift", "slice", "some", "sort",
           "splice", "toReversed", "toSorted", "toSpliced", "toString", "unshift", "values", "with"});
      break;
    case TT::Bytes:
      add({"at", "buffer", "byteLength", "byteOffset", "fill", "forEach", "includes", "indexOf", "join",
           "lastIndexOf", "length", "reverse", "set", "slice", "subarray", "toString"});
      break;
    case TT::ArrayBuffer: add({"byteLength", "slice"}); break;
    case TT::DataView:
      add({"buffer", "byteLength", "byteOffset", "getBigInt64", "getBigUint64", "setBigInt64", "setBigUint64"});
      for (const char* k : {"Uint8", "Int8", "Uint16", "Int16", "Uint32", "Int32", "Float32", "Float64"}) {
        c.push_back(std::string("get") + k);
        c.push_back(std::string("set") + k);
      }
      break;
    case TT::Map: add({"clear", "delete", "entries", "forEach", "get", "has", "keys", "set", "size", "values"}); break;
    case TT::Set: add({"add", "clear", "delete", "entries", "forEach", "has", "keys", "size", "values"}); break;
    case TT::TextEncoder: add({"encode", "encoding"}); break;
    case TT::TextDecoder: add({"decode", "encoding"}); break;
    case TT::Error: add({"message", "name", "toString"}); break;
    case TT::Date:
      add({"getTime", "valueOf", "getTimezoneOffset", "setTime", "toISOString", "toJSON", "toString", "toUTCString",
           "toDateString", "toTimeString"});
      for (const char* g : {"FullYear", "Month", "Date", "Day", "Hours", "Minutes", "Seconds", "Milliseconds"}) {
        c.push_back(std::string("get") + g);
        c.push_back(std::string("getUTC") + g);
      }
      break;
    default: break;
  }
  return didYouMean(name, c);
}

std::string Checker::suggestNamespaceMember(const std::string& ns, const std::string& name) {
  std::vector<std::string> c;
  if (ns == "Math") {
    c = {"PI", "E", "LN2", "LN10", "LOG2E", "LOG10E", "SQRT2", "SQRT1_2", "abs", "ceil", "floor", "round", "trunc",
         "sign", "sqrt", "cbrt", "exp", "expm1", "log", "log1p", "log2", "log10", "sin", "cos", "tan", "asin", "acos",
         "atan", "sinh", "cosh", "tanh", "asinh", "acosh", "atanh", "fround", "clz32", "atan2", "pow", "imul", "hypot",
         "max", "min", "random"};
  } else if (ns == "Number") {
    c = {"MAX_SAFE_INTEGER", "MIN_SAFE_INTEGER", "EPSILON", "MAX_VALUE", "MIN_VALUE", "POSITIVE_INFINITY",
         "NEGATIVE_INFINITY", "NaN", "isFinite", "isInteger", "isNaN", "isSafeInteger", "parseFloat", "parseInt"};
  } else if (ns == "String") {
    c = {"fromCharCode", "fromCodePoint"};
  } else if (ns == "JSON") {
    c = {"parse", "stringify"};
  } else if (ns == "console") {
    c = {"debug", "error", "info", "log", "trace", "warn"};
  } else if (ns == "Object") {
    c = {"freeze", "keys"};
  } else if (ns == "Array") {
    c = {"from", "isArray", "of"};
  } else if (ns == "Uint8Array") {
    c = {"BYTES_PER_ELEMENT", "from", "of"};
  } else if (ns == "BigInt") {
    c = {"asIntN", "asUintN"};
  } else if (ns == "Date") {
    c = {"UTC", "now", "parse"};
  } else {
    c = builtinModuleExports(ns);
  }
  return didYouMean(name, c);
}

std::string Checker::varKey(VarSym* v) {
  char b[32];
  snprintf(b, sizeof b, "#%p", (void*)v);
  return b;
}

// Narrowable reference paths: x, this, x.a.b, this.a
std::string Checker::pathKey(Expr* e, bool* plainConst) {
  while (e->k == EK::Paren) e = e->a;
  if (plainConst) *plainConst = false;
  if (e->k == EK::Ident || e->k == EK::This) {
    VarSym* v = e->var;
    if (!v) return "";
    if (plainConst) *plainConst = v->isConst;
    return varKey(v);
  }
  if (e->k == EK::Member && !e->optional && e->intrinsic == IN_STATIC_FIELD && e->var && !e->cls)
    return varKey(e->var);  // an own static field: a global variable
  if (e->k == EK::Member && !e->optional) {
    std::string base = pathKey(e->a, nullptr);
    if (base.empty()) return "";
    return base + "." + e->raw;
  }
  return "";
}

Type* Checker::narrowed(const std::string& key, bool plainConst) {
  if (key.empty()) return nullptr;
  bool crossed = false;
  for (Scope* s = scope; s; s = s->parent) {
    auto it = s->narrow.find(key);
    if (it != s->narrow.end()) {
      if (!crossed || plainConst) return it->second;
      return nullptr;
    }
    if (s->fnBoundary) crossed = true;
  }
  return nullptr;
}

void Checker::invalidatePath(const std::string& key) {
  if (key.empty()) return;
  std::string prefix = key + ".";
  for (Scope* s = scope; s; s = s->parent) {
    s->narrow[key] = nullptr;
    for (auto& kv : s->narrow)
      if (kv.first.compare(0, prefix.size(), prefix) == 0) kv.second = nullptr;
    if (s->fnBoundary) break;
  }
}

void Checker::markCapture(VarSym* v) {
  if (!v || v->isGlobal || !v->owner || v->owner == curFn) return;
  // only capture if v's owner is a lexical ancestor of curFn
  bool ancestor = false;
  for (FuncSym* f = curFn; f; f = f->parent)
    if (f == v->owner) { ancestor = true; break; }
  if (!ancestor) return;
  v->captured = true;
  bool hoisted = false;  // (read by a nested function declaration)
  for (FuncSym* f = curFn; f && f != v->owner; f = f->parent) {
    hoisted = hoisted || f->selfVar;
    if (!f->captureIndex.count(v)) {
      f->captureIndex[v] = (int)f->captures.size();
      f->captures.push_back(v);
    }
  }
  if (v->initFlag && !hoisted) markCapture(v->initFlag);  // (a switch clause variable: checked everywhere)
  if (hoisted && v->blockVar && !v->isParam) {
    if (!v->initFlag) {
      v->initFlag = P.make<VarSym>();
      v->initFlag->name = v->name + "$init";
      v->initFlag->type = T.boolean();
      v->initFlag->owner = v->owner;
    }
    markCapture(v->initFlag);
  }
}

// ------------------------------------------------------------------ symbols
FuncSym* Checker::newFunc(const std::string& name, std::vector<Param>* params, TypeNode* ret, Stmt* body, Expr* exprBody) {
  FuncSym* f = P.make<FuncSym>();
  f->name = name;
  f->params = params;
  f->retNode = ret;
  f->body = body;
  f->exprBody = exprBody;
  f->file = curFile;
  f->parent = curFn;
  P.funcs.push_back(f);
  return f;
}

VarSym* Checker::newVar(const std::string& name, Type* type, bool isConst, Loc l) {
  VarSym* v = P.make<VarSym>();
  v->name = name;
  v->type = type;
  v->isConst = isConst;
  v->owner = curFn;
  v->loc = l;
  return v;
}

void Checker::declareBuiltins() {
  globalScope = pushScope(false);
  const char* namespaces[] = {"Math", "JSON", "console", "Number", "String", "Array", "Uint8Array", "BigInt",
                              "Object", "Map", "Set", "DataView", "TextEncoder", "TextDecoder", "Error",
                              "ArrayBuffer", "Boolean", "Date", "Symbol", "Promise", "RegExp", "Reflect", "globalThis",
                              "Int8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array", "Int32Array", "Uint32Array",
                              "Float32Array", "Float64Array", "BigInt64Array", "BigUint64Array"};
  for (const char* n : namespaces) {
    NamespaceSym* ns = P.make<NamespaceSym>();
    ns->name = n;
    ns->module = n;
    ns->builtin = true;
    globalScope->names[n] = ns;
  }
  const char* fns[] = {"parseInt", "parseFloat", "isNaN", "isFinite", "encodeURIComponent", "decodeURIComponent",
                       "encodeURI", "decodeURI", "print", "abort", "structuredClone",
                       "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "int",
                       "rotl32", "rotr32", "rotl64", "rotr64", "clz32", "clz64", "ctz32", "ctz64", "popcnt",
                       "bswap16", "bswap32", "bswap64", "mulhi64", "imulhi64", "idiv", "imod"};
  for (const char* n : fns) {
    BuiltinSym* b = P.make<BuiltinSym>();
    b->name = n;
    b->id = n;
    globalScope->names[n] = b;
  }
  // global constants
  {
    VarSym* v = newVar("NaN", T.number(), true, Loc());
    v->isGlobal = true;
    v->globalIdx = -2;  // constant
    globalScope->names["NaN"] = v;
    VarSym* inf = newVar("Infinity", T.number(), true, Loc());
    inf->isGlobal = true;
    inf->globalIdx = -3;
    globalScope->names["Infinity"] = inf;
  }
}

// ------------------------------------------------------------------ run
void Checker::run() {
  declareBuiltins();
  // init function: all top-level code
  FuncSym* init = P.make<FuncSym>();
  init->name = "<init>";
  init->isInit = true;
  init->isTopLevel = true;
  init->type = T.func({}, T.voidT());
  init->sigResolved = true;
  P.funcs.push_back(init);
  P.init = init;

  // declare all files (dependency order)
  for (auto& f : P.files) {
    auto fi = std::make_unique<FileInfo>();
    fi->file = f.get();
    curFile = f->path;
    curFn = init;
    scope = globalScope;
    fi->scope = pushScope(false);
    fi->scope->fn = init;
    fileInfo[f->path] = fi.get();
    fileInfos.push_back(std::move(fi));
  }
  for (auto& fi : fileInfos) {
    curFile = fi->file->path;
    curFileInfo = fi.get();
    scope = fi->scope;
    curFn = init;
    declareFile(fi.get());
  }
  for (auto& fi : fileInfos) {
    curFile = fi->file->path;
    resolveNamespaceRefs(fi.get());
  }
  // resolve declarations
  for (auto& fi : fileInfos) {
    curFile = fi->file->path;
    curFileInfo = fi.get();
    scope = fi->scope;
    curFn = init;
    for (auto& kv : fi->scope->names) {
      Sym* s = kv.second;
      switch (s->k) {
        case SymK::Class: resolveClass(static_cast<ClassSym*>(s)); break;
        case SymK::Interface: resolveInterface(static_cast<InterfaceSym*>(s)); break;
        case SymK::Enum: resolveEnum(static_cast<EnumSym*>(s)); break;
        case SymK::Alias: {
          auto* a = static_cast<AliasSym*>(s);
          if (!a->type) resolveNamedType(a->name, {}, a->loc);
          break;
        }
        default: break;
      }
    }
  }
  for (auto& fi : fileInfos) {
    curFile = fi->file->path;
    curFileInfo = fi.get();
    scope = fi->scope;
    curFn = init;
    for (Stmt* s : fi->file->body)
      if (s->k == SK::Func || s->k == SK::Declare) resolveSig(s->fn);
  }
  // check top-level code in order
  for (auto& fi : fileInfos) {
    curFile = fi->file->path;
    curFileInfo = fi.get();
    scope = fi->scope;
    curFn = init;
    for (Stmt* s : fi->file->body) checkStmt(s);
  }
  // check every function body (including ones not yet reached)
  for (size_t i = 0; i < P.funcs.size(); i++) {
    FuncSym* f = P.funcs[i];
    if (!f->checked && !f->isInit && !f->isNative) checkFunction(f);
  }
  spreadOrders();
  for (Expr* x : iterCalls)
    if (!x->iterUsed)
      err(x->loc, "'" + x->a->raw + "()' is a copy of the elements in E7 (not a live iterator): use it directly in "
                  "for...of, a spread ([...m." + x->a->raw + "()]), Array.from(...) or new Set / new Map, and store "
                  "that array if it is needed later");
  // exports of the entry file
  if (!fileInfos.empty()) {
    FileInfo* entry = fileInfos.back().get();
    curFile = entry->file->path;
    for (auto& kv : entry->exports) {
      if (kv.second->k == SymK::Func) {
        FuncSym* f = static_cast<FuncSym*>(kv.second);
        if (!f->isNative) P.exports.push_back({kv.first, f});
      }
    }
    std::sort(P.exports.begin(), P.exports.end(), [](auto& a, auto& b) { return a.first < b.first; });
  }
}

// ------------------------------------------------------------------ file declarations
void Checker::addExport(FileInfo* fi, const std::string& name, Sym* s, Loc l) {
  auto it = fi->exports.find(name);
  if (it != fi->exports.end() && it->second != s) {
    err(l, name == "default" ? std::string("a module cannot have more than one default export")
                             : "duplicate export '" + name + "'");
    return;
  }
  fi->exports[name] = s;
}

void Checker::declareFile(FileInfo* fi) {
  // a declaration's exports: `export <declaration>` under its name, `export default <declaration>` as "default"
  auto exportDecl = [&](Stmt* s, const std::string& name, Sym* sym, Loc l) {
    if (s->exported) addExport(fi, name, sym, l);
    if (s->isDefault) addExport(fi, "default", sym, l);
  };
  auto declareVars = [&](Stmt* s) {
    for (VarDecl& d : s->decls) {
      if (d.isArrayPat || d.isObjPat) continue;  // typed when checked
      Expr* gi = d.init;
      while (gi && gi->k == EK::Paren) gi = gi->a;
      if (gi && gi->k == EK::Arrow && !gi->typeParams.empty() && !d.type) {
        // const f = <T>(x: T) => ...: a generic function (instantiated per use), not a variable
        if (!s->isConst) { err(d.loc, "a generic arrow function must be assigned to a 'const'"); continue; }
        GenericSym* g = newGeneric(GenericSym::Arrow, d.name, &gi->typeParams, d.loc);
        g->arrow = gi;
        g->parentFn = P.init;
        d.generic = g;
        declare(d.name, g, d.loc);
        exportDecl(s, d.name, g, d.loc);
        continue;
      }
      VarSym* v = newVar(d.name, nullptr, s->isConst, d.loc);
      v->isGlobal = true;
      v->owner = nullptr;
      v->decl = &d;
      v->declStmt = s;
      d.sym = v;
      v->globalIdx = (int)P.globals.size();
      P.globals.push_back(v);
      declare(d.name, v, d.loc);
      exportDecl(s, d.name, v, d.loc);
    }
  };
  std::vector<Stmt*> defaultExprs;  // export default <expression>
  for (Stmt* s : fi->file->body) {
    switch (s->k) {
      case SK::Func: {
        if (!s->typeParams.empty()) {
          GenericSym* g = newGeneric(GenericSym::Func, s->name, &s->typeParams, s->loc);
          g->decl = s;
          s->generic = g;
          declare(s->name, g, s->loc);
          exportDecl(s, s->name, g, s->loc);
          break;
        }
        FuncSym* f = newFunc(s->name, &s->params, s->retType, s->body, nullptr);
        f->isGenerator = s->isGenerator;
        f->isAsync = s->isAsync;
        f->isTopLevel = true;
        f->declStmt = s;
        f->exported = s->exported;
        f->qualName = s->name;
        f->parent = nullptr;
        s->fn = f;
        declare(s->name, f, s->loc);
        exportDecl(s, s->name, f, s->loc);
        break;
      }
      case SK::Declare: {
        FuncSym* f = newFunc(s->name, &s->params, s->retType, nullptr, nullptr);
        f->isNative = true;
        f->isTopLevel = true;
        f->parent = nullptr;
        f->declStmt = s;
        s->fn = f;
        declare(s->name, f, s->loc);
        exportDecl(s, s->name, f, s->loc);
        break;
      }
      case SK::Class: {
        if (!s->typeParams.empty()) {
          // instances (Box<number>) are created on use; the static members belong to the holder
          GenericSym* g = newGeneric(GenericSym::Class, s->name, &s->typeParams, s->loc);
          g->decl = s;
          ClassSym* h = P.make<ClassSym>();
          h->name = s->name;
          h->decl = s;
          h->file = curFile;
          h->type = T.classType(h);
          h->genericRole = 1;
          g->holder = h;
          s->cls = h;
          s->generic = g;
          declare(s->name, g, s->loc);
          exportDecl(s, s->name, g, s->loc);
          break;
        }
        ClassSym* c = P.make<ClassSym>();
        c->name = s->name;
        c->decl = s;
        c->file = curFile;
        c->type = T.classType(c);
        s->cls = c;
        declare(s->name, c, s->loc);
        exportDecl(s, s->name, c, s->loc);
        break;
      }
      case SK::Interface: {
        if (!s->typeParams.empty()) {
          GenericSym* g = newGeneric(GenericSym::Interface, s->name, &s->typeParams, s->loc);
          g->decl = s;
          s->generic = g;
          declare(s->name, g, s->loc);
          exportDecl(s, s->name, g, s->loc);
          break;
        }
        // declaration merging: a second `interface X` in the same scope adds its members to the first
        if (Sym* prev = scope->names.count(s->name) ? scope->names[s->name] : nullptr;
            prev && prev->k == SymK::Interface && static_cast<InterfaceSym*>(prev)->state == 0) {
          auto* first = static_cast<InterfaceSym*>(prev);
          TypeNode* fb = first->decl->typeBody;
          for (auto& m : s->typeBody->members) fb->members.push_back(m);
          for (TypeNode* x : s->ifaceExtends) first->decl->ifaceExtends.push_back(x);
          break;
        }
        InterfaceSym* is = P.make<InterfaceSym>();
        is->name = s->name;
        is->decl = s;
        declare(s->name, is, s->loc);
        exportDecl(s, s->name, is, s->loc);
        break;
      }
      case SK::TypeAlias: {
        if (!s->typeParams.empty()) {
          GenericSym* g = newGeneric(GenericSym::Alias, s->name, &s->typeParams, s->loc);
          g->decl = s;
          s->generic = g;
          declare(s->name, g, s->loc);
          exportDecl(s, s->name, g, s->loc);
          break;
        }
        AliasSym* a = P.make<AliasSym>();
        a->name = s->name;
        a->decl = s;
        declare(s->name, a, s->loc);
        exportDecl(s, s->name, a, s->loc);
        break;
      }
      case SK::Enum: {
        EnumSym* e = P.make<EnumSym>();
        e->name = s->name;
        e->decl = s;
        e->type = T.enumType(e);
        declare(s->name, e, s->loc);
        exportDecl(s, s->name, e, s->loc);
        break;
      }
      case SK::Var:
        if (s->isDefault) defaultExprs.push_back(s);  // after the other declarations (it may name one)
        else declareVars(s);
        break;
      case SK::Import:
        bindImports(fi, s);
        break;
      case SK::Export:
        break;
      default:
        break;
    }
  }
  // export default <expression>: a declaration of this module (other than a `let`, whose current value the
  // export copies) is exported as itself, like `export { name as default }`; any other expression is
  // evaluated at that point into the hidden const "default"
  for (Stmt* s : defaultExprs) {
    Expr* x = s->decls[0].init;
    if (x->k == EK::Ident) {
      auto it = fi->scope->names.find(x->raw);
      if (it != fi->scope->names.end() &&
          !(it->second->k == SymK::Var && !static_cast<VarSym*>(it->second)->isConst)) {
        ImportSpec sp;
        sp.name = x->raw;
        sp.alias = "default";
        sp.loc = x->loc;
        s->k = SK::Export;
        s->imports = {sp};
        continue;  // exported with the export lists below
      }
    }
    declareVars(s);
  }
  // export { a, b as c }, export { a, b as c } from "./x", export * as ns from "./x"
  for (Stmt* s : fi->file->body) {
    if (s->k != SK::Export || s->starExport) continue;
    if (s->from.empty()) {
      for (auto& sp : s->imports) {
        auto it = fi->scope->names.find(sp.name);
        if (it == fi->scope->names.end()) { err(s->loc, "cannot export unknown name '" + sp.name + "'"); continue; }
        addExport(fi, sp.alias.empty() ? sp.name : sp.alias, it->second, s->loc);
        if (it->second->k == SymK::Func) static_cast<FuncSym*>(it->second)->exported = true;
      }
      continue;
    }
    FileInfo* tfi = nullptr;
    if (!importTarget(s, tfi)) continue;
    if (!s->nsAlias.empty()) addExport(fi, s->nsAlias, moduleNamespace(s, tfi, s->nsAlias), s->loc);
    for (auto& sp : s->imports) {
      const std::string& as = sp.alias.empty() ? sp.name : sp.alias;
      if (Sym* m = importedSym(s, tfi, sp.name, as)) addExport(fi, as, m, s->loc);
    }
  }
  // export * from "./x": every export of x but "default", unless this module exports the name itself. A name
  // that two `export *` provide with different bindings is ambiguous: an error, as in TypeScript (JavaScript
  // leaves it out of the module)
  auto sameBinding = [](Sym* a, Sym* b) {
    if (a == b) return true;
    if (a->k == SymK::Builtin && b->k == SymK::Builtin)
      return static_cast<BuiltinSym*>(a)->id == static_cast<BuiltinSym*>(b)->id;
    return a->k == SymK::Alias && b->k == SymK::Alias && static_cast<AliasSym*>(a)->type &&
           static_cast<AliasSym*>(a)->type == static_cast<AliasSym*>(b)->type;
  };
  std::map<std::string, std::pair<Sym*, std::string>> starred;  // name -> (binding, module it came from)
  for (Stmt* s : fi->file->body) {
    if (s->k != SK::Export || !s->starExport) continue;
    FileInfo* tfi = nullptr;
    if (!importTarget(s, tfi)) continue;
    std::map<std::string, Sym*> names;  // sorted: deterministic messages
    if (tfi) {
      names.insert(tfi->exports.begin(), tfi->exports.end());
    } else {
      for (const std::string& n : builtinModuleExports(s->from))
        if (Sym* m = importedSym(s, nullptr, n, n)) names[n] = m;
    }
    for (auto& kv : names) {
      if (kv.first == "default" || fi->exports.count(kv.first)) continue;
      auto it = starred.find(kv.first);
      if (it == starred.end()) starred[kv.first] = {kv.second, s->from};
      else if (!sameBinding(it->second.first, kv.second))
        err(s->loc, "module '" + it->second.second + "' has already exported a member named '" + kv.first +
                        "'; re-export it explicitly to resolve the ambiguity");
    }
  }
  for (auto& kv : starred) fi->exports[kv.first] = kv.second.first;
}

// The module an import or a re-export names: tfi is its file, or null for a builtin module (e7:...)
bool Checker::importTarget(Stmt* s, FileInfo*& tfi) {
  tfi = nullptr;
  if (s->from.rfind("e7:", 0) == 0) {
    if (s->from != "e7:crypto" && s->from != "e7:encoding" && s->from != "e7:json" && s->from != "e7:test") {
      std::string alt = closestName(s->from, builtinModuleNames());
      err(s->loc, "unknown builtin module '" + s->from + "'" +
                      (alt.empty() ? "; the builtin modules are e7:crypto, e7:encoding, e7:json and e7:test"
                                   : "; did you mean '" + alt + "'?"));
      return false;
    }
    return true;
  }
  SourceFile* target = resolveModule(s->from, curFile);
  if (!target) { err(s->loc, "cannot find module '" + s->from + "'"); return false; }
  auto it = fileInfo.find(target->path);
  if (it == fileInfo.end()) { err(s->loc, "cannot find module '" + s->from + "'"); return false; }
  tfi = it->second;
  return true;
}

// The binding of export `name` of the module (tfi, or the builtin module s->from when null), made available
// as `alias`; null (reported) when the module has no such export
Sym* Checker::importedSym(Stmt* s, FileInfo* tfi, const std::string& name, const std::string& alias) {
  if (!tfi) {
    const auto& names = builtinModuleExports(s->from);
    if (std::find(names.begin(), names.end(), name) == names.end()) {
      if (name == "default") err(s->loc, "module '" + s->from + "' has no default export");
      else {
        std::string hint = removedBuiltinHint(s->from, name);
        err(s->loc, "module '" + s->from + "' has no exported member '" + name + "'" +
                        (hint.empty() ? didYouMean(name, names) : hint));
      }
      return nullptr;
    }
    if (name == "KeyHandle") {
      AliasSym* a = P.make<AliasSym>();
      a->name = alias;
      a->type = T.handle();
      a->state = 2;
      return a;
    }
    BuiltinSym* b = P.make<BuiltinSym>();
    b->name = alias;
    b->id = s->from + "." + name;
    return b;
  }
  // the target was declared first (dependency order), so its exports are complete
  auto e = tfi->exports.find(name);
  if (e == tfi->exports.end()) {
    if (name == "default") {
      err(s->loc, "module '" + s->from + "' has no default export");
      return nullptr;
    }
    std::vector<std::string> names;
    for (auto& kv : tfi->exports) names.push_back(kv.first);
    err(s->loc, "module '" + s->from + "' has no exported member '" + name + "'" + didYouMean(name, names));
    return nullptr;
  }
  return e->second;
}

// `import * as alias` / `export * as alias`: the module as a namespace
NamespaceSym* Checker::moduleNamespace(Stmt* s, FileInfo* tfi, const std::string& alias) {
  NamespaceSym* ns = P.make<NamespaceSym>();
  ns->name = alias;
  if (!tfi) {
    ns->module = s->from;
    ns->builtin = true;
  } else {
    ns->module = tfi->file->path;
    ns->members = tfi->exports;  // complete: the target was declared first (dependency order)
  }
  return ns;
}

void Checker::bindImports(FileInfo* fi, Stmt* s) {
  (void)fi;
  FileInfo* tfi = nullptr;
  if (!importTarget(s, tfi)) return;
  // import "./x" binds nothing (the module still runs, in dependency order)
  if (!s->nsAlias.empty()) declare(s->nsAlias, moduleNamespace(s, tfi, s->nsAlias), s->loc);
  for (auto& sp : s->imports) {
    Sym* m = importedSym(s, tfi, sp.name, sp.alias);
    if (!m) continue;
    if (s->typeOnly || sp.typeOnly) {
      // `import type { C }`: the name is a type only (tsc rejects a value use; the import is erased)
      Type* t = nullptr;
      if (m->k == SymK::Class) t = static_cast<ClassSym*>(m)->type;
      else if (m->k == SymK::Enum) {
        resolveEnum(static_cast<EnumSym*>(m));
        t = static_cast<EnumSym*>(m)->type;
      } else if (m->k == SymK::Var || m->k == SymK::Func || m->k == SymK::Builtin) {
        continue;  // a value imported as a type: nothing to use without `typeof` types
      }
      if (t) {
        AliasSym* a = P.make<AliasSym>();
        a->name = sp.alias;
        a->type = t;
        a->state = 2;
        a->typeOnlyImport = true;
        m = a;
      }
    }
    declare(sp.alias, m, s->loc);
  }
}

// ------------------------------------------------------------------ type resolution
Type* Checker::resolveType(TypeNode* n) {
  if (!n) return T.unknown();
  switch (n->k) {
    case TNK::Ref: return resolveNamedType(n->name, n->args, n->loc);
    case TNK::Array: return T.array(resolveType(n->elem), n->readonly);
    case TNK::Tuple: {
      std::vector<Type*> es;
      for (TypeNode* a : n->args) es.push_back(resolveType(a));
      if (es.empty()) return errT(n->loc, "empty tuple types are not supported");
      return T.tuple(es, n->readonly);
    }
    case TNK::Func: {
      std::vector<Type*> ps;
      std::vector<bool> opt;
      bool rest = false;
      for (size_t i = 0; i < n->params.size(); i++) {
        auto& p = n->params[i];
        Type* pt = resolveType(p.type);
        if (p.rest) {
          if (i + 1 != n->params.size()) err(n->loc, "rest parameter must be last");
          if (pt->t != TT::Array) err(n->loc, "rest parameter must be an array type");
          rest = true;
        }
        if (p.optional) pt = T.optional(pt, false, true);
        ps.push_back(pt);
        opt.push_back(p.optional);
      }
      return T.func(ps, resolveType(n->ret), opt, rest);
    }
    case TNK::Object: {
      std::vector<ObjShape::Prop> props;
      bool hasMethods = false;
      for (auto& m : n->members) if (m.method) hasMethods = true;
      if (!hasMethods) {
        for (auto& m : n->members) {
          Type* pt = resolveType(m.type);
          bool uo = m.optional && hasUndefined(pt);
          if (m.optional) pt = T.optional(pt, false, true);
          std::string name = m.name;
          if (m.computed && !constKey(m.computed, name)) continue;
          props.push_back({name, pt, m.optional, uo, m.readonly});
        }
        return T.anonObject(props);
      }
      T.shapes.emplace_back(new ObjShape());
      ObjShape* sh = T.shapes.back().get();
      for (auto& m : n->members) {
        Type* pt = resolveType(m.type);
        if (m.method) sh->methods.push_back({m.accessor == 1 ? "get " + m.name : m.accessor == 2 ? "set " + m.name : m.name, pt});
        else {
          bool uo = m.optional && hasUndefined(pt);
          if (m.optional) pt = T.optional(pt, false, true);
          sh->props.push_back({m.name, pt, m.optional, uo, m.readonly});
        }
      }
      return T.objectType(sh);
    }
    case TNK::Union: {
      bool hasNull = false, hasUndef = false;
      Type* base = nullptr;
      // unions of literals of one base type ("a" | "b", 0 | 1, true | false) are literal types; a literal
      // with its base type is the base type
      auto join = [&](Type* t) -> bool {
        if (!base || base == t) { base = t; return true; }
        if (base->lit && t->lit) {
          if (Type* u = T.litUnion(base, t)) { base = u; return true; }
          return false;
        }
        if (baseLit(base) == baseLit(t) && (base->lit || t->lit)) { base = baseLit(base); return true; }
        return false;
      };
      for (TypeNode* a : n->args) {
        Type* t = resolveType(a);
        if (t->t == TT::Null) hasNull = true;
        else if (t->t == TT::Undefined) hasUndef = true;
        else if (t->t == TT::Optional) {
          hasNull |= t->hasNull;
          hasUndef |= t->hasUndef;
          if (!join(t->elem)) return errT(n->loc, "union types other than 'T | null | undefined' are not supported (E7 types are static; a union of literals must have one base type)");
        } else if (!join(t)) {
          return errT(n->loc, "union types other than 'T | null | undefined' are not supported (E7 types are static; a union of literals must have one base type)");
        }
      }
      if (!base) return hasNull ? T.nullT() : T.undef();
      return T.optional(base, hasNull, hasUndef);
    }
    case TNK::Literal:
      if (n->numLiteral) return T.numLit({n->num});
      return T.strLit({utf8ToUtf16(n->literal)});
  }
  return T.unknown();
}

Type* Checker::resolveNamedType(const std::string& name, const std::vector<TypeNode*>& args, Loc l) {
  auto noArgs = [&]() {
    if (!args.empty()) err(l, "type '" + name + "' is not generic");
  };
  auto nargs = [&](size_t n) -> bool {
    if (args.size() != n) { err(l, "type '" + name + "' requires " + std::to_string(n) + " type argument(s)"); return false; }
    return true;
  };
  // user-defined types shadow builtins
  Sym* s = lookup(name);
  if (s) {
    switch (s->k) {
      case SymK::Class: {
        noArgs();
        return static_cast<ClassSym*>(s)->type;
      }
      case SymK::Interface: {
        noArgs();
        auto* is = static_cast<InterfaceSym*>(s);
        resolveInterface(is);
        return is->type ? is->type : T.unknown();
      }
      case SymK::Alias: {
        noArgs();
        auto* a = static_cast<AliasSym*>(s);
        if (a->state == 1) {
          if (a->type) return a->type;  // a recursive object type alias (below)
          return errT(l, "type alias '" + name + "' circularly references itself");
        }
        if (!a->type) {
          a->state = 1;
          std::string savedFile = curFile;
          TypeNode* body = a->decl->typeBody;
          bool hasMethods = false;
          if (body->k == TNK::Object)
            for (auto& m : body->members) hasMethods = hasMethods || m.method;
          if (body->k == TNK::Object && !hasMethods && typeRefersTo(body, a->name)) {
            // type Tree = { children: Tree[] }: a named shape (like an interface), created before its members
            T.shapes.emplace_back(new ObjShape());
            ObjShape* sh = T.shapes.back().get();
            sh->name = a->name;
            a->type = T.objectType(sh);
            for (auto& m : body->members) {
              Type* pt = resolveType(m.type);
              bool uo = m.optional && hasUndefined(pt);
              if (m.optional) pt = T.optional(pt, false, true);
              sh->props.push_back({m.name, pt, m.optional, uo, m.readonly});
            }
          } else {
            a->type = resolveType(body);
          }
          // name anonymous object types after alias for messages
          if (a->type->t == TT::Object && a->type->shape->name.empty()) {
            // keep structural identity; just remember a display name when unique
          }
          curFile = savedFile;
          a->state = 2;
        }
        return a->type;
      }
      case SymK::Enum: {
        noArgs();
        auto* e = static_cast<EnumSym*>(s);
        resolveEnum(e);
        return e->type;
      }
      case SymK::Generic:
        return genericNamedType(static_cast<GenericSym*>(s), args, l);
      default: break;
    }
  }
  size_t dot = name.rfind('.');
  if (dot != std::string::npos) {
    // ns.T: a type exported by a module namespace (resolved by lookup above)
    Sym* base = lookup(name.substr(0, dot));
    // E.A: the type of an enum member (e7: the enum's type; tsc checks the member)
    if (base && base->k == SymK::Enum) {
      auto* en = static_cast<EnumSym*>(base);
      resolveEnum(en);
      std::string member = name.substr(dot + 1);
      for (auto& m : en->members)
        if (m.name == member) { noArgs(); return en->type; }
      return errT(l, "enum '" + en->name + "' has no member '" + member + "'");
    }
    if (base && base->k == SymK::Namespace && !static_cast<NamespaceSym*>(base)->builtin) {
      if (s) return errT(l, "'" + name + "' is not a type");
      std::string member = name.substr(dot + 1);
      std::vector<std::string> names;
      for (auto& kv : static_cast<NamespaceSym*>(base)->members) names.push_back(kv.first);
      return errT(l, "module '" + name.substr(0, dot) + "' has no exported member '" + member + "'" +
                         didYouMean(member, names));
    }
  }
  if (name == "number") { noArgs(); return T.number(); }
  if (name == "string") { noArgs(); return T.string(); }
  if (name == "boolean") { noArgs(); return T.boolean(); }
  if (name == "bigint") { noArgs(); return T.bigint(); }
  if (name == "void") { noArgs(); return T.voidT(); }
  if (name == "true" || name == "false") { noArgs(); return T.boolLit(name == "true" ? 2 : 1); }
  if (name == "this") {
    // the polymorphic `this` type: the class (a method returning `this` gives the receiver's type at a call)
    if (curThisClass) { noArgs(); return curThisClass->type; }
    return errT(l, "a 'this' type is only available in classes");
  }
  if (name == "NoInfer" && args.size() == 1) return resolveType(args[0]);  // only affects tsc's inference
  if (name == "Awaited" && args.size() == 1) {
    Type* t = resolveType(args[0]);
    return t->t == TT::Promise ? t->elem : t;
  }
  if (name == "NonNullable" && args.size() == 1) {
    Type* t = resolveType(args[0]);
    if (t->t == TT::Null || t->t == TT::Undefined) return T.never();
    return baseOf(t);
  }
  if ((name == "Uppercase" || name == "Lowercase" || name == "Capitalize" || name == "Uncapitalize") && args.size() == 1) {
    // on string literal types (ASCII letters change case as in JavaScript); on string, string
    Type* t = resolveType(args[0]);
    if (t->t != TT::String) return errT(l, name + " needs a string type");
    if (!t->lit) return t;
    std::vector<std::u16string> out;
    for (auto v : t->strLits) {
      for (size_t k = 0; k < v.size(); k++) {
        char16_t& c = v[k];
        bool all = name == "Uppercase" || name == "Lowercase";
        if (!all && k > 0) break;
        bool up = name == "Uppercase" || name == "Capitalize";
        if (up && c >= 'a' && c <= 'z') c = (char16_t)(c - 32);
        if (!up && c >= 'A' && c <= 'Z') c = (char16_t)(c + 32);
      }
      out.push_back(v);
    }
    return T.strLit(out);
  }
  if (name == "null") { noArgs(); return T.nullT(); }
  if (name == "undefined") { noArgs(); return T.undef(); }
  if (name == "never") { noArgs(); return T.never(); }
  if (name == "any" || name == "unknown" || name == "object" || name == "Object" || name == "Function" || name == "symbol")
    return errT(l, "type '" + name + "' is not supported in E7 (all types must be static); use a concrete type, an "
                   "interface, a union with null, or Map<K, V> for dynamic keys");
  static const std::pair<const char*, uint8_t> ints[] = {{"i8", VT_I8}, {"u8", VT_U8}, {"i16", VT_I16}, {"u16", VT_U16},
                                                          {"i32", VT_I32}, {"u32", VT_U32}, {"i64", VT_I64}, {"u64", VT_U64},
                                                          {"int", VT_I64}};
  for (auto& p : ints)
    if (name == p.first) { noArgs(); return T.intT(p.second); }
  if (name == "Array" || name == "ReadonlyArray") {
    if (!nargs(1)) return T.unknown();
    return T.array(resolveType(args[0]), name == "ReadonlyArray");
  }
  if (name == "Map" || name == "ReadonlyMap") {
    if (!nargs(2)) return T.unknown();
    return T.map(resolveType(args[0]), resolveType(args[1]), name == "ReadonlyMap");
  }
  if (name == "Set" || name == "ReadonlySet") {
    if (!nargs(1)) return T.unknown();
    return T.set(resolveType(args[0]), name == "ReadonlySet");
  }
  if (name == "Uint8Array") { noArgs(); return T.bytes(); }
  if (name == "ArrayBuffer") { noArgs(); return T.arrayBuffer(); }
  if (name == "DataView") { noArgs(); return T.dataView(); }
  if (name == "KeyHandle" || name == "crypto.KeyHandle") { noArgs(); return T.handle(); }
  {
    // ns.KeyHandle through any namespace import of e7:crypto (import * as c from "e7:crypto")
    size_t dot = name.find('.');
    if (dot != std::string::npos && name.compare(dot + 1, std::string::npos, "KeyHandle") == 0) {
      Sym* ns = lookup(name.substr(0, dot));
      if (ns && ns->k == SymK::Namespace && static_cast<NamespaceSym*>(ns)->builtin &&
          static_cast<NamespaceSym*>(ns)->module == "e7:crypto") {
        noArgs();
        return T.handle();
      }
    }
  }
  if (name == "TextEncoder") { noArgs(); return T.textEncoder(); }
  if (name == "TextDecoder") { noArgs(); return T.textDecoder(); }
  // (the error classes are one type in e7: an Error, whose name says which)
  if (name == "Error" || name == "AggregateError" || name == "TypeError" || name == "RangeError" || name == "SyntaxError" ||
      name == "URIError" || name == "ReferenceError" || name == "EvalError") { noArgs(); return T.error(); }
  if (name == "TemplateStringsArray") { noArgs(); return T.array(T.string(), true); }
  if (name == "Date") { noArgs(); return T.date(); }
  if (name == "RegExp" || name == "RegExpMatchArray" || name == "RegExpExecArray") return errT(l, kNoRegExp);
  if (name == "Record" || name == "Partial" || name == "Required" || name == "Pick" || name == "Omit" || name == "Readonly")
    return errT(l, "utility type '" + name + "' is not supported; use Map<K, V> or an interface");
  if (name == "Generator" || name == "Iterator" || name == "IterableIterator" || name == "IteratorObject" ||
      name == "Generator" || name == "IterableIterator") {
    uint8_t gk = name == "Generator" ? 0 : name == "Iterator" ? 1 : name == "IterableIterator" ? 2 : 3;
    size_t maxArgs = gk == 2 ? 1 : 3;
    if (args.empty() || args.size() > maxArgs) {
      err(l, "type '" + name + "' requires 1" + (maxArgs > 1 ? " to " + std::to_string(maxArgs) : std::string()) + " type argument(s)");
      return T.unknown();
    }
    Type* y = resolveType(args[0]);
    Type* r = args.size() > 1 ? genTypeArg(args[1]) : T.voidT();
    Type* n = args.size() > 2 ? genTypeArg(args[2]) : T.voidT();
    if (y->t == TT::Void || y->t == TT::Undefined) y = T.undef();
    return T.generator(y, r, n, gk);
  }
  if (name == "AsyncGenerator" || name == "AsyncIterator" || name == "AsyncIterableIterator" || name == "AsyncIterable") {
    // all are async generators at run time
    uint8_t gk = name == "AsyncGenerator" ? 0 : name == "AsyncIterator" ? 1 : name == "AsyncIterableIterator" ? 2 : 3;
    if (args.empty() || args.size() > 3) {
      err(l, "type '" + name + "' requires 1 to 3 type argument(s)");
      return T.unknown();
    }
    Type* y = resolveType(args[0]);
    Type* r = args.size() > 1 ? genTypeArg(args[1]) : T.voidT();
    Type* n = args.size() > 2 ? genTypeArg(args[2]) : T.voidT();
    if (y->t == TT::Void || y->t == TT::Undefined) y = T.undef();
    if (y->t == TT::Promise) return errT(l, "an async generator cannot yield promises (yield awaits them): use '" + typeName(y->elem) + "'");
    return T.asyncGenerator(y, r, n, gk);
  }
  if (name == "IteratorResult" || name == "IteratorYieldResult" || name == "IteratorReturnResult") {
    if (args.empty() || args.size() > 2) return errT(l, "type '" + name + "' requires 1 or 2 type argument(s)");
    Type* y = resolveType(args[0]);
    Type* r = args.size() > 1 ? genTypeArg(args[1]) : T.voidT();
    return iterResultType(T.generator(y, r, T.voidT()), l);
  }
  if (name == "Promise" || name == "PromiseLike") {
    if (!nargs(1)) return T.unknown();
    Type* e = resolveType(args[0]);
    if (e->t == TT::Undefined) e = T.voidT();
    if (e->t == TT::Promise) e = e->elem;  // Promise<Promise<T>> is Promise<T>
    return T.promise(e);
  }
  if (int k = typedArrayKind(name)) { noArgs(); return T.typedArray((uint8_t)k); }
  return errT(l, "unknown type '" + name + "'" + suggestName(name, true));
}

// TReturn / TNext of a generator type: `void`, `undefined`, `unknown` and `any` mean "no value"
Type* Checker::genTypeArg(TypeNode* n) {
  if (n->k == TNK::Ref && n->args.empty() &&
      (n->name == "void" || n->name == "undefined" || n->name == "unknown" || n->name == "any"))
    return T.voidT();
  Type* t = resolveType(n);
  return t->t == TT::Undefined ? T.voidT() : t;
}

// IteratorResult<T, TReturn> of a generator type: { value: T; done: boolean } when TReturn is T or
// none (then value is T | undefined); any other TReturn would make `value` a general union
Type* Checker::iterResultType(Type* g, Loc l) {
  Type* v = g->elem;
  if (g->ret && g->ret->t != TT::Void && g->ret != g->elem) {
    if (!(assignable(g->ret, g->elem) && kindOf(g->ret) == kindOf(g->elem) && !isBoxedOptional(g->elem) &&
          isBoxedOptional(g->ret) == isBoxedOptional(g->elem)))
      err(l, "the result of next() on '" + typeName(g) + "' would be a union of '" + typeName(g->elem) + "' and '" +
                 typeName(g->ret) + "' (E7 types are static); give the generator the return type '" + typeName(g->elem) +
                 "' or void");
  } else if (!g->ret || g->ret->t == TT::Void) {
    v = T.optional(g->elem, false, true);
  }
  return T.anonObject({{"value", v, false}, {"done", T.boolean(), false}});
}

// whether a type expression mentions the type named `name` (a recursive type alias)
bool Checker::typeRefersTo(TypeNode* n, const std::string& name) {
  if (!n) return false;
  if (n->k == TNK::Ref && n->name == name) return true;
  for (TypeNode* a : n->args) if (typeRefersTo(a, name)) return true;
  if (typeRefersTo(n->elem, name) || typeRefersTo(n->ret, name)) return true;
  for (auto& p : n->params) if (typeRefersTo(p.type, name)) return true;
  for (auto& m : n->members) if (typeRefersTo(m.type, name)) return true;
  return false;
}

void Checker::resolveInterface(InterfaceSym* is) {
  if (is->state == 2) return;
  if (is->state == 1) {
    // a member refers back to the interface (directly or through other types): its type exists already
    if (is->inExtends) err(is->loc, "interface '" + is->name + "' circularly extends itself");
    return;
  }
  is->state = 1;
  Scope* savedScope = scope;
  if (is->defScope) scope = is->defScope;  // instance of a generic interface: type parameters bound
  T.shapes.emplace_back(new ObjShape());
  ObjShape* sh = T.shapes.back().get();
  sh->name = is->name;
  sh->origin = is->origin;
  sh->typeArgs = is->typeArgs;
  is->type = T.objectType(sh);
  for (TypeNode* ext : is->decl->ifaceExtends) {
    is->inExtends = true;
    Type* pt = resolveType(ext);
    is->inExtends = false;
    if (pt->t != TT::Object) { err(ext->loc, "an interface can only extend interfaces"); continue; }
    for (auto& p : pt->shape->props) sh->props.push_back(p);
    for (auto& m : pt->shape->methods) sh->methods.push_back(m);
  }
  for (auto& m : is->decl->typeBody->members)
    if (m.computed) {
      std::string key;
      Expr* k = m.computed;
      m.computed = nullptr;
      if (constKey(k, key)) m.name = key;
    }
  for (auto& m : is->decl->typeBody->members) {
    Type* pt = resolveType(m.type);
    if (sh->findProp(m.name) || sh->findMethod(m.name)) {
      // allow identical redeclaration
    }
    if (m.method) sh->methods.push_back({m.accessor == 1 ? "get " + m.name : m.accessor == 2 ? "set " + m.name : m.name, pt});
    else {
      bool uo = m.optional && hasUndefined(pt);
      if (m.optional) pt = T.optional(pt, false, true);
      sh->props.push_back({m.name, pt, m.optional, uo, m.readonly});
    }
  }
  scope = savedScope;
  is->state = 2;
}

static int32_t toInt32(double d) {
  if (!std::isfinite(d)) return 0;
  double t = std::trunc(d);
  double m = std::fmod(t, 4294967296.0);
  if (m < 0) m += 4294967296.0;
  return (int32_t)(uint32_t)m;
}

bool Checker::enumConst(Expr* x, EnumSym* cur, double& num, std::u16string& str, bool& isStr) {
  isStr = false;
  switch (x->k) {
    case EK::Num: num = x->num; return true;
    case EK::Str: str = x->str; isStr = true; return true;
    case EK::Paren: return enumConst(x->a, cur, num, str, isStr);
    case EK::Template:
      if (!x->list.empty() || x->quasis.size() != 1) return false;
      str = x->quasis[0];
      isStr = true;
      return true;
    case EK::Ident: {
      for (auto& m : cur->members)
        if (m.name == x->raw) {
          isStr = cur->isString || !m.str.empty();
          if (isStr) str = m.str; else num = m.num;
          return true;
        }
      return false;
    }
    case EK::Member: {
      Expr* b = x->a;
      while (b->k == EK::Paren) b = b->a;
      if (b->k != EK::Ident) return false;
      Sym* s = lookup(b->raw);
      if (!s || s->k != SymK::Enum) return false;
      EnumSym* other = static_cast<EnumSym*>(s);
      if (other != cur) resolveEnum(other);
      for (auto& m : other->members)
        if (m.name == x->raw) {
          isStr = !m.str.empty() || other->isString;
          if (isStr) str = m.str; else num = m.num;
          return true;
        }
      return false;
    }
    case EK::Unary: {
      double a;
      std::u16string sa;
      bool as;
      if (!enumConst(x->a, cur, a, sa, as) || as) return false;
      if (x->raw == "-") num = -a;
      else if (x->raw == "+") num = a;
      else if (x->raw == "~") num = (double)~toInt32(a);
      else return false;
      return true;
    }
    case EK::Binary: {
      double a = 0, b = 0;
      std::u16string sa, sb;
      bool as = false, bs = false;
      if (!enumConst(x->a, cur, a, sa, as) || !enumConst(x->b, cur, b, sb, bs)) return false;
      const std::string& op = x->raw;
      if (as || bs) {
        if (op != "+" || !as || !bs) return false;  // string members: only concatenation of strings
        str = sa + sb;
        isStr = true;
        return true;
      }
      if (op == "+") num = a + b;
      else if (op == "-") num = a - b;
      else if (op == "*") num = a * b;
      else if (op == "/") num = a / b;
      else if (op == "%") num = std::fmod(a, b);
      else if (op == "**") num = std::pow(a, b);
      else if (op == "|") num = (double)(toInt32(a) | toInt32(b));
      else if (op == "&") num = (double)(toInt32(a) & toInt32(b));
      else if (op == "^") num = (double)(toInt32(a) ^ toInt32(b));
      else if (op == "<<") num = (double)(int32_t)((uint32_t)toInt32(a) << (toInt32(b) & 31));
      else if (op == ">>") num = (double)(toInt32(a) >> (toInt32(b) & 31));
      else if (op == ">>>") num = (double)((uint32_t)toInt32(a) >> (toInt32(b) & 31));
      else return false;
      return true;
    }
    default: return false;
  }
}

void Checker::resolveEnum(EnumSym* e) {
  if (e->state) return;
  e->state = 1;
  double next = 0;
  bool anyStr = false, anyNum = false;
  for (auto& m : e->decl->enumMembers) {
    EnumSym::Member mem;
    mem.name = m.name;
    if (m.init) {
      // a constant expression (as in tsc): literals, earlier members (bare or E.X), members of other enums,
      // parentheses, unary + - ~, binary arithmetic and bitwise operators, string concatenation
      double num = 0;
      std::u16string str;
      bool isStr = false;
      if (!enumConst(m.init, e, num, str, isStr)) {
        err(m.init->loc, "enum member initializers must be constant expressions (literals, other members, and "
                         "arithmetic, bitwise or + operators on them); computed members are not supported");
      } else if (isStr) {
        mem.str = str;
        anyStr = true;
      } else {
        mem.num = num;
        next = mem.num + 1;
        anyNum = true;
      }
    } else {
      if (anyStr) err(e->decl->loc, "enum member '" + m.name + "' must have an initializer");
      mem.num = next++;
      anyNum = true;
    }
    for (auto& o : e->members)
      if (o.name == m.name) err(e->decl->loc, "duplicate enum member '" + m.name + "'");
    e->members.push_back(mem);
  }
  if (anyStr && anyNum) err(e->decl->loc, "heterogeneous enums are not supported");
  e->isString = anyStr;
  e->state = 2;
}

void Checker::resolveClass(ClassSym* c) {
  if (c->state != 0) return;  // resolved, or being resolved (partial layout is visible)
  c->state = 1;
  Stmt* d = c->decl;
  std::string savedFile = curFile;
  curFile = c->file;
  Scope* savedScope = scope;
  FuncSym* savedFn = curFn;
  // generic classes: the holder has the static members, an instance the instance members (with the
  // type parameters bound in its scope)
  bool holder = c->genericRole == 1, inst = c->genericRole == 2;
  if (c->defScope) scope = c->defScope;
  int savedNest = instNest;
  if (inst) instNest = std::max(instNest, c->instDepth);
  auto instInfo = [&](FuncSym* f) {
    if (!inst) return;
    f->defScope = c->defScope;
    f->instName = f->qualName;
    f->instDepth = c->instDepth;
  };
  // computed member names `[K]`: constants known at compile time
  for (auto& m : d->members)
    if (m.computed) {
      std::string key;
      FuncSym* f1 = curFn;
      curFn = nullptr;
      if (constKey(m.computed, key)) m.name = key;
      curFn = f1;
      m.computed = nullptr;
    }
  if (d->extends && !holder) {
    Type* pt = resolveType(d->extends);
    if (pt->t != TT::Class) err(d->extends->loc, "a class can only extend another class");
    else if (pt->cls->state == 1 || pt->cls == c) err(d->extends->loc, "class '" + c->name + "' circularly extends itself");
    else {
      resolveClass(pt->cls);
      c->parent = pt->cls;
      c->fields = pt->cls->fields;
      c->presence = pt->cls->presence;  // (its instances have the base class's absent fields)
      c->vtable = pt->cls->vtable;
      c->fieldIdx = pt->cls->fieldIdx;
      c->methodIdx = pt->cls->methodIdx;
    }
  }
  // static members are looked up in the base class next (a generic base: its static holder)
  if (d->extends) {
    ClassSym* sp = c->parent;
    if (holder && d->extends->k == TNK::Ref) sp = classOf(lookup(d->extends->name));
    if (sp && sp->genericRole == 2 && sp->origin) sp = sp->origin->holder;
    if (sp && sp != c && sp->state != 1) {
      resolveClass(sp);
      c->staticParent = sp;
    }
  }
  for (TypeNode* im : d->implements) {
    if (holder) break;
    Type* it = resolveType(im);
    if (it->t != TT::Object) err(im->loc, "a class can only implement interfaces");
    else c->implements.push_back(it);
  }
  // constructor symbol (explicit or synthesized)
  ClassMember* ctorDecl = nullptr;
  for (auto& m : d->members)
    if (m.kind == ClassMember::Ctor && !holder) {
      if (ctorDecl) err(m.loc, "multiple constructors are not supported");
      ctorDecl = &m;
    }
  curFn = nullptr;
  FuncSym* ctor = newFunc(c->name + ".constructor", ctorDecl ? &ctorDecl->params : nullptr, nullptr,
                          ctorDecl ? ctorDecl->body : nullptr, nullptr);
  ctor->parent = nullptr;
  ctor->cls = c;
  ctor->isCtor = true;
  ctor->isMethod = true;
  ctor->isTopLevel = true;
  ctor->qualName = c->name + ".constructor";
  c->ctor = ctor;
  if (ctorDecl) ctorDecl->fn = ctor;
  ctor->thisVar = newVar("this", c->type, true, d->loc);
  ctor->thisVar->isThis = true;
  ctor->thisVar->owner = ctor;
  instInfo(ctor);
  if (!ctorDecl && c->parent) {
    // implicit `constructor(...args) { super(...args); }`
    resolveSig(c->parent->ctor);
    ctor->inheritCtor = true;
    ctor->sigResolved = true;
    ctor->hasDefaults = c->parent->ctor->hasDefaults;
    ctor->type = c->parent->ctor->type;
  }

  c->isAbstract = d->isAbstract;
  if (c->parent) c->abstractProps = c->parent->abstractProps;
  auto addField = [&](const std::string& name, Type* t, Expr* init, bool optional, Loc l, bool readonly, bool undefOk) {
    if (name[0] == '#' && c->fieldIdx.count(name) && c->fields[c->fieldIdx[name]].owner != c) {
      err(l, "private field '" + name + "' is also declared by a base class; e7 needs distinct private names in a class hierarchy");
      return;
    }
    if (auto fi = c->fieldIdx.find(name); fi != c->fieldIdx.end() && c->fields[fi->second].owner != c && name[0] != '#') {
      // a field of the base class declared again (class C extends A { name = "C" }): the same property,
      // which C's initializer sets after A's (with the type it has: its representation is fixed)
      auto& f = c->fields[fi->second];
      if (t != f.type) {
        err(l, "field '" + name + "' must have the type of the inherited field ('" + typeName(f.type) + "') in e7 (the field's representation is fixed)");
        return;
      }
      f.init = init;
      f.owner = c;
      f.loc = l;
      f.readonly = readonly;
      f.absent = false;  // (an absent interface property of the base class: declared here)
      return;
    }
    if (c->fieldIdx.count(name) || c->methodIdx.count(name) || c->privateMethods.count(name)) {
      err(l, "duplicate member '" + name + "' in class '" + c->name + "'");
      return;
    }
    if (c->getterSlot(name) >= 0 || c->setterSlot(name) >= 0) {
      err(l, "'" + name + "' is an accessor in class '" + c->name + "' (or a base class); it cannot also be a field");
      return;
    }
    if (Type* at = c->abstractProp(name)) {
      // implements an abstract property of a base class (read by name through the base type)
      if (at != t) err(l, "property '" + name + "' must have the type of the abstract property ('" + typeName(at) + "')");
      c->abstractProps.erase(name);
    }
    c->fieldIdx[name] = (int)c->fields.size();
    c->fields.push_back({name, t, init, optional, c, l, readonly, undefOk});
  };
  // parameter properties come first (TS emits them first in the constructor)
  if (ctorDecl) {
    for (auto& p : ctorDecl->params) {
      if (!p.isProperty) continue;
      if (!p.type) { err(p.loc, "parameter property '" + p.name + "' needs a type annotation"); continue; }
      Type* t = resolveType(p.type);
      bool uo = p.optional && hasUndefined(t);
      if (p.optional) t = T.optional(t, false, true);
      addField(p.name, t, nullptr, p.optional, p.loc, p.readonly, uo);
    }
  }
  // methods; accessors are methods named "get x" / "set x" (not valid identifiers, so they cannot
  // clash with other members); #private methods are not virtual
  for (auto& m : d->members) {
    if (m.kind != ClassMember::Method) continue;
    if ((holder && !m.isStatic) || (inst && m.isStatic)) continue;
    if (!m.typeParams.empty()) {
      // generic method: instantiated per call, called directly (not virtual)
      if (m.isAbstract) { err(m.loc, "a generic method cannot be abstract (e7 calls generic methods directly)"); continue; }
      if (m.name[0] == '#') { err(m.loc, "private generic methods are not supported; use a generic function"); continue; }
      bool clash = c->methodIdx.count(m.name) > 0 || c->genericMethods.count(m.name) > 0;
      for (ClassSym* p = c->parent; p && !clash; p = p->parent) clash = p->genericMethods.count(m.name) > 0;
      if (clash || c->fieldIdx.count(m.name)) {
        err(m.loc, "generic method '" + m.name + "' cannot override or be overridden (e7 calls generic methods directly, not through the class)");
        continue;
      }
      GenericSym* g = newGeneric(GenericSym::Method, m.name, &m.typeParams, m.loc);
      g->member = &m;
      g->ownerClass = c;
      g->isStatic = m.isStatic;
      g->file = c->file;
      c->genericMethods[m.name] = g;
      continue;
    }
    if (!m.isStatic)
      for (ClassSym* p = c->parent; p; p = p->parent)
        if (p->genericMethods.count(m.name))
          err(m.loc, "method '" + m.name + "' cannot override the generic method of class '" + p->name +
                         "' (e7 calls generic methods directly, not through the class)");
    // a setter without a parameter type takes the getter's return type
    if (m.accessor == 2 && m.params.size() == 1 && !m.params[0].type) {
      for (auto& g : d->members)
        if (g.kind == ClassMember::Method && g.accessor == 1 && g.name == m.name && g.isStatic == m.isStatic && g.retType)
          m.params[0].type = g.retType;
    }
    std::string key = m.accessor == 1 ? "get " + m.name : m.accessor == 2 ? "set " + m.name : m.name;
    curFn = nullptr;
    FuncSym* f = newFunc(key, &m.params, m.retType, m.body, nullptr);
    f->isGenerator = m.isGenerator;
    f->isAsync = m.isAsync;
    if ((m.isGenerator || m.isAsync) && (m.accessor || m.kind == ClassMember::Ctor))
      err(m.loc, "an accessor or constructor cannot be a generator or async");
    f->loc = m.loc;
    f->parent = nullptr;
    f->cls = c;
    f->isTopLevel = true;
    f->qualName = c->name + "." + key;
    f->isStatic = m.isStatic;
    f->accessor = m.accessor;
    f->isAbstract = m.isAbstract;
    m.fn = f;
    instInfo(f);
    if (m.isAbstract) {
      if (!c->isAbstract) err(m.loc, "abstract methods can only appear within an abstract class");
      if (m.isStatic) err(m.loc, "'static' and 'abstract' cannot be used together");
      if (m.name[0] == '#') err(m.loc, "an abstract member cannot be private (#)");
      if (!m.retType && m.accessor != 2) err(m.loc, "abstract method '" + m.name + "' needs a return type annotation");
    }
    if (m.accessor == 2) f->retNode = nullptr;
    if (m.isStatic) {
      if (c->staticMethods.count(key) || c->staticFields.count(key) ||
          (m.accessor && c->staticMethods.count(m.name)) ||
          (!m.accessor && (c->staticMethods.count("get " + m.name) || c->staticMethods.count("set " + m.name))))
        err(m.loc, "duplicate static member '" + m.name + "'");
      c->staticMethods[key] = f;
      continue;
    }
    f->isMethod = true;
    f->thisVar = newVar("this", c->type, true, m.loc);
    f->thisVar->isThis = true;
    f->thisVar->owner = f;
    if (c->fieldIdx.count(m.name)) {
      err(m.loc, m.accessor ? "accessor '" + m.name + "' conflicts with a field of class '" + c->name + "' (or a base class)"
                            : "member '" + m.name + "' conflicts with a field");
      continue;
    }
    if (c->abstractProp(m.name)) {
      err(m.loc, "abstract property '" + m.name + "' must be implemented as a field (e7 reads it by name)");
      continue;
    }
    if (m.name[0] == '#') {
      if (c->privateMethods.count(key)) { err(m.loc, "duplicate method '" + m.name + "'"); continue; }
      for (ClassSym* p = c->parent; p; p = p->parent)
        if (p->privateMethods.count(key) || p->fieldIdx.count(key))
          err(m.loc, "private method '" + m.name + "' is also declared by a base class; e7 needs distinct private names in a class hierarchy");
      c->privateMethods[key] = f;
      continue;
    }
    if (m.accessor ? c->methodIdx.count(m.name) > 0 : (c->getterSlot(m.name) >= 0 || c->setterSlot(m.name) >= 0)) {
      err(m.loc, "'" + m.name + "' is " + (m.accessor ? "a method" : "an accessor") + " in class '" + c->name +
                     "' (or a base class); it cannot also be " + (m.accessor ? "an accessor" : "a method"));
      continue;
    }
    auto it = c->methodIdx.find(key);
    if (it != c->methodIdx.end()) {
      if (c->vtable[it->second].fn->cls == c) { err(m.loc, "duplicate " + std::string(m.accessor ? "accessor" : "method") + " '" + m.name + "'"); continue; }
      c->vtable[it->second].fn = f;  // override, signature checked below
    } else {
      c->methodIdx[key] = (int)c->vtable.size();
      c->vtable.push_back({key, f});
    }
  }
  // an accessor declared in a subclass replaces both halves of the base class's: overriding only the
  // getter leaves no setter in JavaScript (and only the setter no getter)
  for (auto& m : d->members) {
    if (m.kind != ClassMember::Method || !m.accessor || m.isStatic || m.name[0] == '#') continue;
    if ((holder && !m.isStatic) || (inst && m.isStatic)) continue;
    std::string other = (m.accessor == 1 ? "set " : "get ") + m.name;
    auto it = c->methodIdx.find(other);
    if (it == c->methodIdx.end() || c->vtable[it->second].fn->cls == c) continue;
    err(m.loc, "class '" + c->name + "' declares only the " + (m.accessor == 1 ? "getter" : "setter") + " of '" + m.name +
                   "', which replaces the whole accessor of '" + c->vtable[it->second].fn->cls->name + "' (it would have no " +
                   (m.accessor == 1 ? "setter" : "getter") + "); declare both, the other calling super." + m.name);
  }
  // `declare x: T` fields last: they exist once the constructor assigns them, after the initialized
  // fields (the key order of JavaScript for the usual `declare` + assignment in the constructor)
  std::vector<ClassMember*> fieldOrder;
  for (auto& m : d->members)
    if (!m.isDeclare) fieldOrder.push_back(&m);
  for (auto& m : d->members)
    if (m.isDeclare) fieldOrder.push_back(&m);
  for (ClassMember* mp : fieldOrder) {
    ClassMember& m = *mp;
    if (m.kind != ClassMember::Field) continue;
    if ((holder && !m.isStatic) || (inst && m.isStatic)) continue;
    if (m.isAbstract) {
      // abstract property: no slot here; subclasses declare it as a field
      if (!c->isAbstract) err(m.loc, "abstract properties can only appear within an abstract class");
      if (m.isStatic) { err(m.loc, "'static' and 'abstract' cannot be used together"); continue; }
      if (m.name[0] == '#') { err(m.loc, "an abstract member cannot be private (#)"); continue; }
      if (!m.type) { err(m.loc, "abstract property '" + m.name + "' needs a type annotation"); continue; }
      if (c->fieldIdx.count(m.name) || c->methodIdx.count(m.name) || c->getterSlot(m.name) >= 0 ||
          c->setterSlot(m.name) >= 0 || c->abstractProps.count(m.name)) {
        err(m.loc, "duplicate member '" + m.name + "' in class '" + c->name + "'");
        continue;
      }
      Type* t = resolveType(m.type);
      if (m.optional) t = T.optional(t, false, true);
      c->abstractProps[m.name] = t;
      continue;
    }
    if (m.isStatic) {
      VarSym* v = newVar(c->name + "." + m.name, nullptr, false, m.loc);
      v->isGlobal = true;
      v->isConst = m.readonly;  // `static readonly`: only its initializer writes it
      v->owner = nullptr;
      v->globalIdx = (int)P.globals.size();
      P.globals.push_back(v);
      if (m.type) {
        v->type = resolveType(m.type);
        if (m.optional) v->type = T.optional(v->type, false, true);
      }
      // initializer checked lazily (in init context)
      VarDecl* vd = new VarDecl();
      vd->name = m.name;
      vd->type = m.type;
      vd->init = m.init;
      vd->loc = m.loc;
      P.arena.decls.emplace_back(vd);  // owned by this compilation (was a static vector: a data race, and a leak)
      v->decl = vd;
      if (c->staticFields.count(m.name) || c->staticMethods.count(m.name) || c->staticMethods.count("get " + m.name) ||
          c->staticMethods.count("set " + m.name))
        err(m.loc, "duplicate static member '" + m.name + "'");
      c->staticFields[m.name] = v;
      continue;
    }
    Type* t = nullptr;
    bool uo = false;
    if (m.type) {
      t = resolveType(m.type);
      uo = m.optional && hasUndefined(t);
      if (m.optional) t = T.optional(t, false, true);
    } else if (m.init) {
      // infer from initializer, checked in constructor context
      Scope* s0 = scope;
      FuncSym* f0 = curFn;
      curFn = ctor;
      pushScope(true);
      declare("this", ctor->thisVar, m.loc);
      t = widen(check(m.init, nullptr));
      scope = s0;
      curFn = f0;
    } else {
      err(m.loc, "field '" + m.name + "' needs a type annotation or initializer");
      t = T.unknown();
    }
    if (m.isDeclare && c->fieldIdx.count(m.name) && c->fields[c->fieldIdx[m.name]].owner != c) {
      // `declare x: T` of an inherited field: a declaration for the type checker, the same field
      Type* inherited = c->fields[c->fieldIdx[m.name]].type;
      if (t != inherited)
        err(m.loc, "a 'declare' field must have the type of the inherited field ('" + typeName(inherited) + "') in e7 (the field's representation is fixed)");
      continue;
    }
    if (m.isDeclare && m.init) err(m.loc, "a 'declare' field cannot have an initializer");
    addField(m.name, t, m.init, m.optional, m.loc, m.readonly, uo);
  }
  if (!c->defScope) scope = savedScope;
  curFn = savedFn;
  c->state = 2;
  c->classIdx = (int)P.classes.size();
  P.classes.push_back(c);
  // resolve method signatures and check overrides
  for (auto& m : d->members)
    if (m.fn && m.kind == ClassMember::Method) resolveSig(m.fn);
  if (c->parent) {
    for (size_t k = 0; k < c->parent->vtable.size(); k++) {
      FuncSym* base = c->parent->vtable[k].fn;
      FuncSym* ov = c->vtable[k].fn;
      if (base == ov) continue;
      resolveSig(base);
      resolveSig(ov);
      if (ov->inferRet && !ov->checked) { ov->retInferred = true; checkFunction(ov); }  // (its return type, inferred from its body)
      if (base->inferRet && !base->checked) checkFunction(base);
      // an inferred return type that the overridden method's takes, with the same representation (a
      // Promise<string> for a Promise<string | null>): the overridden method's (as tsc checks it)
      if (ov->retInferred && base->type && ov->type && ov->type->ret != base->type->ret) {
        Type* a = ov->type->ret;
        Type* b = base->type->ret;
        bool ok = kindOf(a) == kindOf(b) && isBoxedOptional(a) == isBoxedOptional(b) && assignable(a, b);
        if (!ok && a->t == TT::Promise && b->t == TT::Promise)
          ok = kindOf(a->elem) == kindOf(b->elem) && isBoxedOptional(a->elem) == isBoxedOptional(b->elem) && assignable(a->elem, b->elem);
        if (ok) ov->type = T.func(ov->type->params, b, ov->type->optParams, ov->type->restParam);
      }
      if (base->type && ov->type && base->type != ov->type && ov->type->ret && base->type->ret) {
        // a compatible override (as TypeScript allows): fewer parameters (the others are passed and
        // ignored) with the same representations, and a return type assignable to the overridden one's
        Type* bt = base->type;
        Type* ot = ov->type;
        if (ot->ret->t == TT::Class) resolveClass(ot->ret->cls);  // its parents decide the assignability
        bool ok = !base->hasDefaults && !ov->hasDefaults && !ot->restParam && !bt->restParam &&
                  ot->params.size() <= bt->params.size() + 0 && kindOf(ot->ret) == kindOf(bt->ret) &&
                  isBoxedOptional(ot->ret) == isBoxedOptional(bt->ret) && assignable(ot->ret, bt->ret);
        for (size_t i = 0; ok && i < ot->params.size(); i++) {
          Type* a = ot->params[i];
          Type* b = bt->params[i];
          ok = kindOf(a) == kindOf(b) && isBoxedOptional(a) == isBoxedOptional(b) && (assignable(b, a) || assignable(a, b)) &&
               ot->optParams[i] == bt->optParams[i];
        }
        if (!ok) {
          err(ov->loc, "method '" + ov->name + "' must have a signature compatible with the overridden method (" +
                           typeName(base->type) + ")");
        } else {
          ov->padParams.assign(bt->params.begin() + (std::ptrdiff_t)ot->params.size(), bt->params.end());
          ov->padParams.insert(ov->padParams.end(), base->padParams.begin(), base->padParams.end());
        }
      } else if (base->type == ov->type) {
        ov->padParams = base->padParams;
      }
    }
  }
  // a getter and its setter have the same type
  for (auto& m : d->members) {
    if (m.kind != ClassMember::Method || m.accessor != 2 || !m.fn || !m.fn->type) continue;
    FuncSym* g = nullptr;
    if (m.isStatic) {
      auto it = c->staticMethods.find("get " + m.name);
      if (it != c->staticMethods.end()) g = it->second;
    } else if (c->getterSlot(m.name) >= 0) {
      g = c->vtable[c->getterSlot(m.name)].fn;
    }
    if (!g) continue;
    resolveSig(g);
    Type* st = m.fn->type->params.empty() ? nullptr : m.fn->type->params[0];
    if (g->type && !g->inferRet && st && st != g->type->ret && st->t != TT::Unknown)
      err(m.loc, "the getter and setter of '" + m.name + "' must have the same type ('" + typeName(g->type->ret) +
                     "' and '" + typeName(st) + "')");
  }
  // a concrete class implements every abstract member
  if (!c->isAbstract) {
    for (auto& vm : c->vtable)
      if (vm.fn->isAbstract) {
        std::string nm = vm.fn->accessor ? vm.name.substr(4) : vm.name;
        err(d->loc, "non-abstract class '" + c->name + "' does not implement inherited abstract member '" + nm +
                        "' from class '" + vm.fn->cls->name + "'");
      }
    for (auto& ap : c->abstractProps)
      err(d->loc, "non-abstract class '" + c->name + "' does not implement inherited abstract property '" + ap.first +
                      "' (declare it as a field)");
  }
  // implements: an optional property of the interface that the class does not declare is an absent
  // field (class Fish implements Animal { name = "x" } with Animal's legs?: number: read through the
  // interface, fish.legs is undefined; it is not an own property until it is written)
  if (!c->isRecord && !holder)
    for (Type* it : c->implements) {
      if (it->t != TT::Object || !it->shape) continue;
      for (auto& p : it->shape->props) {
        if (!p.optional || c->fieldIdx.count(p.name) || c->methodIdx.count(p.name) || c->getterSlot(p.name) >= 0 ||
            c->setterSlot(p.name) >= 0 || c->abstractProp(p.name) || p.name[0] == '#')
          continue;
        Type* ft = p.type->t == TT::Optional ? p.type : T.optional(p.type, false, true);
        c->fieldIdx[p.name] = (int)c->fields.size();
        c->fields.push_back({p.name, ft, nullptr, true, c, d->loc, p.readonly, p.undefOk, true});
        c->presence = true;
      }
    }
  // implements: structural check
  for (Type* it : c->implements) {
    if (!structurallyAssignable(c->type, it)) {
      std::string why;
      for (auto& p : it->shape->props) {
        if (auto fi = c->fieldIdx.find(p.name); fi != c->fieldIdx.end()) {
          Type* ft = c->fields[fi->second].type;
          if (ft != p.type && p.type->t == TT::Optional && baseOf(p.type) == baseOf(ft) && ft->t != TT::Optional) {
            why = ": property '" + p.name + "' is optional in the interface; e7 stores it the way the interface reads it, so declare it as '" +
                  p.name + "?: " + typeName(ft) + "' in the class";
            break;
          }
          continue;
        }
        if (c->abstractProp(p.name)) continue;
        if (c->methodIdx.count(p.name) && baseOf(p.type)->t == TT::Func) {
          why = ": '" + p.name + "' is a method of the class, and the interface's '" + p.name + "' is " +
                (p.optional ? "an optional method (a property that may hold a function)" : "a property holding a function") +
                "; e7 reads it as a property: declare it in the class as '" + p.name + " = (...) => ...'";
          break;
        }
        if (c->getterSlot(p.name) >= 0)
          why = ": property '" + p.name + "' is an accessor in the class; e7 reads interface properties as fields, so declare it in the interface as 'get " +
                p.name + "(): " + typeName(p.type) + "'";
        else
          why = ": property '" + p.name + "' is missing";
        break;
      }
      err(d->loc, "class '" + c->name + "' does not correctly implement '" + typeName(it) + "'" + why);
    }
  }
  scope = savedScope;
  instNest = savedNest;
  curFile = savedFile;
}

void Checker::resolveSig(FuncSym* f) {
  if (!f || f->sigResolved) return;
  f->sigResolved = true;
  std::string savedFile = curFile;
  curFile = f->file.empty() ? curFile : f->file;
  Scope* savedSigScope = scope;
  if (f->defScope) scope = f->defScope;  // e.g. the type parameters of a generic instance
  std::vector<Type*> ps;
  std::vector<bool> opt;
  bool rest = false;
  std::vector<Param>* params = f->params;
  bool seenOptional = false;
  if (params) {
    for (size_t i = 0; i < params->size(); i++) {
      Param& p = (*params)[i];
      Type* t;
      if (p.type) t = resolveType(p.type);
      else if (p.init) {
        // type of the default value; it may refer to the earlier parameters (as for arrow functions)
        Scope* s0 = scope;
        pushScope(true);
        if (f->thisVar) scope->names["this"] = f->thisVar;
        for (size_t k = 0; k < i; k++) {
          Param& q = (*params)[k];
          if (q.pattern || !ps[k]) continue;
          VarSym* tv = newVar(q.name, ps[k], false, q.loc);
          tv->owner = f;
          scope->names[q.name] = tv;
        }
        t = widen(check(p.init, nullptr));
        scope = s0;
      } else if (f->arrow) {
        t = nullptr;  // contextual, filled by checkArrow
      } else {
        err(p.loc, "parameter '" + p.name + "' needs a type annotation");
        t = T.unknown();
      }
      if (p.rest) {
        if (i + 1 != params->size()) err(p.loc, "rest parameter must be last");
        if (t && t->t != TT::Array) err(p.loc, "rest parameter must have an array type");
        rest = true;
      }
      bool isOpt = p.optional || p.init;
      if (p.init && !p.optional && t && t->t == TT::Optional && t->hasUndef) {
        // f(x: T | undefined = d, y: U): an argument is needed for x (it may be undefined: then d)
        bool laterRequired = false;
        for (size_t k = i + 1; k < params->size(); k++)
          laterRequired = laterRequired || (!(*params)[k].optional && !(*params)[k].init && !(*params)[k].rest);
        if (laterRequired) isOpt = false;
      }
      if (p.init) f->hasDefaults = true;
      if (p.optional && p.init) err(p.loc, "parameter cannot be optional and have an initializer");
      if (p.optional && t) t = T.optional(t, false, true);
      if (isOpt) seenOptional = true;
      else if (seenOptional && !p.rest) err(p.loc, "a required parameter cannot follow an optional parameter");
      if (!isOpt && p.init) seenOptional = false;  // (x: T | undefined = d before a required parameter)
      ps.push_back(t);
      opt.push_back(isOpt);
    }
  }
  ClassSym* savedThis = curThisClass;
  curThisClass = f->cls;
  Type* ret = f->retNode ? resolveType(f->retNode) : nullptr;
  curThisClass = savedThis;
  f->retThis = f->cls && f->retNode && f->retNode->k == TNK::Ref && f->retNode->name == "this";
  if (f->isCtor || f->accessor == 2) ret = T.voidT();
  if (f->retNode && !f->retNode->predParam.empty()) {
    TypeNode* rn = f->retNode;
    f->asserts = rn->asserts;
    f->predParam = -1;
    if (rn->predParam == "this" && f->thisVar) f->predParam = -2;
    if (params)
      for (size_t k = 0; k < params->size(); k++)
        if ((*params)[k].name == rn->predParam) f->predParam = (int)k;
    if (f->predParam == -1) err(rn->loc, "cannot find parameter '" + rn->predParam + "'");
    if (rn->predType) {
      f->predType = resolveType(rn->predType);
      Type* pt = f->predParam >= 0 && (size_t)f->predParam < ps.size() ? ps[(size_t)f->predParam] : nullptr;
      if (pt && !assignable(f->predType, pt))
        err(rn->predType->loc, "a type predicate's type must be assignable to its parameter's type ('" + typeName(f->predType) +
                                   "' is not a '" + typeName(pt) + "')");
    }
  }
  if (ret && (f->isGenerator || f->isAsync)) checkCoroutineRet(f, ret, f->retNode ? f->retNode->loc : f->loc);
  // an async host function: declare function f(...): Promise<T> (the host settles a ticket)
  bool asyncNative = f->isNative && ret && ret->t == TT::Promise;
  if (asyncNative) ret = ret->elem;
  if (f->isNative) {
    if (!ret) ret = T.voidT();
    for (Type* t : ps)
      if (t && t->t != TT::Number && t->t != TT::Bool && t->t != TT::String && t->t != TT::Int && t->t != TT::Bytes &&
          t->t != TT::Handle)
        err(f->loc, "native function parameters must be number, boolean, string, integer, Uint8Array or KeyHandle");
    if (ret->t != TT::Void && ret->t != TT::Number && ret->t != TT::Bool && ret->t != TT::String && ret->t != TT::Int &&
        ret->t != TT::Bytes)
      err(f->loc, "native function return type must be void, number, boolean, string, integer or Uint8Array (or a Promise of one)");
  }
  if (asyncNative) ret = T.promise(ret);
  std::vector<Type*> pp;
  for (Type* t : ps) pp.push_back(t ? t : T.unknown());
  if (!ret) { f->inferRet = true; ret = T.unknown(); }
  f->type = T.func(pp, ret, opt, rest);
  curFile = savedFile;
  scope = savedSigScope;
}

}  // namespace e7
