#include <algorithm>
#include "checker.h"
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
  if (scope->names.count(name)) {
    err(l, "duplicate declaration of '" + name + "'");
    return;
  }
  s->loc = l;
  scope->names[name] = s;
}

Sym* Checker::lookup(const std::string& name, bool* crossedFn) {
  bool crossed = false;
  for (Scope* s = scope; s; s = s->parent) {
    auto it = s->names.find(name);
    if (it != s->names.end()) {
      if (crossedFn) *crossedFn = crossed;
      return it->second;
    }
    if (s->fnBoundary) crossed = true;
  }
  return nullptr;
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
           "splice", "toString", "unshift", "values"});
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
  for (FuncSym* f = curFn; f && f != v->owner; f = f->parent) {
    if (!f->captureIndex.count(v)) {
      f->captureIndex[v] = (int)f->captures.size();
      f->captures.push_back(v);
    }
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
                              "ArrayBuffer", "Boolean", "Date", "Symbol", "Promise", "RegExp", "Reflect", "globalThis"};
  for (const char* n : namespaces) {
    NamespaceSym* ns = P.make<NamespaceSym>();
    ns->name = n;
    ns->module = n;
    ns->builtin = true;
    globalScope->names[n] = ns;
  }
  const char* fns[] = {"parseInt", "parseFloat", "isNaN", "isFinite", "encodeURIComponent", "decodeURIComponent",
                       "encodeURI", "decodeURI", "print", "abort",
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
void Checker::declareFile(FileInfo* fi) {
  for (Stmt* s : fi->file->body) {
    switch (s->k) {
      case SK::Func: {
        if (!s->typeParams.empty()) {
          GenericSym* g = newGeneric(GenericSym::Func, s->name, &s->typeParams, s->loc);
          g->decl = s;
          s->generic = g;
          declare(s->name, g, s->loc);
          if (s->exported) fi->exports[s->name] = g;
          break;
        }
        FuncSym* f = newFunc(s->name, &s->params, s->retType, s->body, nullptr);
        f->isTopLevel = true;
        f->declStmt = s;
        f->exported = s->exported;
        f->qualName = s->name;
        f->parent = nullptr;
        s->fn = f;
        declare(s->name, f, s->loc);
        if (s->exported) fi->exports[s->name] = f;
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
        if (s->exported) fi->exports[s->name] = f;
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
          if (s->exported) fi->exports[s->name] = g;
          break;
        }
        ClassSym* c = P.make<ClassSym>();
        c->name = s->name;
        c->decl = s;
        c->file = curFile;
        c->type = T.classType(c);
        s->cls = c;
        declare(s->name, c, s->loc);
        if (s->exported) fi->exports[s->name] = c;
        break;
      }
      case SK::Interface: {
        if (!s->typeParams.empty()) {
          GenericSym* g = newGeneric(GenericSym::Interface, s->name, &s->typeParams, s->loc);
          g->decl = s;
          s->generic = g;
          declare(s->name, g, s->loc);
          if (s->exported) fi->exports[s->name] = g;
          break;
        }
        InterfaceSym* is = P.make<InterfaceSym>();
        is->name = s->name;
        is->decl = s;
        declare(s->name, is, s->loc);
        if (s->exported) fi->exports[s->name] = is;
        break;
      }
      case SK::TypeAlias: {
        if (!s->typeParams.empty()) {
          GenericSym* g = newGeneric(GenericSym::Alias, s->name, &s->typeParams, s->loc);
          g->decl = s;
          s->generic = g;
          declare(s->name, g, s->loc);
          if (s->exported) fi->exports[s->name] = g;
          break;
        }
        AliasSym* a = P.make<AliasSym>();
        a->name = s->name;
        a->decl = s;
        declare(s->name, a, s->loc);
        if (s->exported) fi->exports[s->name] = a;
        break;
      }
      case SK::Enum: {
        EnumSym* e = P.make<EnumSym>();
        e->name = s->name;
        e->decl = s;
        e->type = T.enumType(e);
        declare(s->name, e, s->loc);
        if (s->exported) fi->exports[s->name] = e;
        break;
      }
      case SK::Var: {
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
            if (s->exported) fi->exports[d.name] = g;
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
          if (s->exported) fi->exports[d.name] = v;
        }
        break;
      }
      case SK::Import:
        bindImports(fi, s);
        break;
      case SK::Export:
        break;
      default:
        break;
    }
  }
  // export { a, b as c }
  for (Stmt* s : fi->file->body) {
    if (s->k != SK::Export) continue;
    for (auto& sp : s->imports) {
      auto it = fi->scope->names.find(sp.name);
      if (it == fi->scope->names.end()) { err(s->loc, "cannot export unknown name '" + sp.name + "'"); continue; }
      fi->exports[sp.alias.empty() ? sp.name : sp.alias] = it->second;
      if (it->second->k == SymK::Func) static_cast<FuncSym*>(it->second)->exported = true;
    }
  }
}

void Checker::bindImports(FileInfo* fi, Stmt* s) {
  bool builtinMod = s->from.rfind("e7:", 0) == 0;
  if (builtinMod) {
    if (s->from != "e7:crypto" && s->from != "e7:encoding" && s->from != "e7:json" && s->from != "e7:test") {
      std::string alt = closestName(s->from, builtinModuleNames());
      err(s->loc, "unknown builtin module '" + s->from + "'" +
                      (alt.empty() ? "; the builtin modules are e7:crypto, e7:encoding, e7:json and e7:test"
                                   : "; did you mean '" + alt + "'?"));
      return;
    }
    if (!s->nsAlias.empty()) {
      NamespaceSym* ns = P.make<NamespaceSym>();
      ns->name = s->nsAlias;
      ns->module = s->from;
      ns->builtin = true;
      declare(s->nsAlias, ns, s->loc);
      return;
    }
    for (auto& sp : s->imports) {
      const auto& names = builtinModuleExports(s->from);
      if (std::find(names.begin(), names.end(), sp.name) == names.end()) {
        err(s->loc, "module '" + s->from + "' has no exported member '" + sp.name + "'" + didYouMean(sp.name, names));
        continue;
      }
      if (sp.name == "KeyHandle") {
        AliasSym* a = P.make<AliasSym>();
        a->name = sp.alias;
        a->type = T.handle();
        a->state = 2;
        declare(sp.alias, a, s->loc);
        continue;
      }
      BuiltinSym* b = P.make<BuiltinSym>();
      b->name = sp.alias;
      b->id = s->from + "." + sp.name;
      declare(sp.alias, b, s->loc);
    }
    return;
  }
  SourceFile* target = resolveModule(s->from, curFile);
  if (!target) { err(s->loc, "cannot find module '" + s->from + "'"); return; }
  auto it = fileInfo.find(target->path);
  if (it == fileInfo.end()) { err(s->loc, "cannot find module '" + s->from + "'"); return; }
  FileInfo* tfi = it->second;
  if (!s->nsAlias.empty()) {
    NamespaceSym* ns = P.make<NamespaceSym>();
    ns->name = s->nsAlias;
    ns->module = target->path;
    ns->members = tfi->exports;  // copied after target declared (dependency order)
    declare(s->nsAlias, ns, s->loc);
    return;
  }
  for (auto& sp : s->imports) {
    auto e = tfi->exports.find(sp.name);
    if (e == tfi->exports.end()) {
      std::vector<std::string> names;
      for (auto& kv : tfi->exports) names.push_back(kv.first);
      err(s->loc, "module '" + s->from + "' has no exported member '" + sp.name + "'" + didYouMean(sp.name, names));
      continue;
    }
    declare(sp.alias, e->second, s->loc);
  }
}

// ------------------------------------------------------------------ type resolution
Type* Checker::resolveType(TypeNode* n) {
  if (!n) return T.unknown();
  switch (n->k) {
    case TNK::Ref: return resolveNamedType(n->name, n->args, n->loc);
    case TNK::Array: return T.array(resolveType(n->elem));
    case TNK::Tuple: {
      std::vector<Type*> es;
      for (TypeNode* a : n->args) es.push_back(resolveType(a));
      if (es.empty()) return errT(n->loc, "empty tuple types are not supported");
      return T.tuple(es);
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
          if (m.optional) pt = T.optional(pt, false, true);
          props.push_back({m.name, pt, m.optional});
        }
        return T.anonObject(props);
      }
      T.shapes.emplace_back(new ObjShape());
      ObjShape* sh = T.shapes.back().get();
      for (auto& m : n->members) {
        Type* pt = resolveType(m.type);
        if (m.method) sh->methods.push_back({m.accessor == 1 ? "get " + m.name : m.accessor == 2 ? "set " + m.name : m.name, pt});
        else {
          if (m.optional) pt = T.optional(pt, false, true);
          sh->props.push_back({m.name, pt, m.optional});
        }
      }
      return T.objectType(sh);
    }
    case TNK::Union: {
      bool hasNull = false, hasUndef = false;
      Type* base = nullptr;
      bool literalStrings = false;
      for (TypeNode* a : n->args) {
        Type* t = (a->k == TNK::Literal) ? (literalStrings = true, T.string()) : resolveType(a);
        if (t->t == TT::Null) hasNull = true;
        else if (t->t == TT::Undefined) hasUndef = true;
        else if (t->t == TT::Optional) {
          hasNull |= t->hasNull;
          hasUndef |= t->hasUndef;
          if (base && base != t->elem) return errT(n->loc, "union types other than 'T | null | undefined' are not supported");
          base = t->elem;
        } else {
          if (base && base != t) return errT(n->loc, "union types other than 'T | null | undefined' are not supported");
          base = t;
        }
      }
      (void)literalStrings;
      if (!base) return hasNull ? T.nullT() : T.undef();
      return T.optional(base, hasNull, hasUndef);
    }
    case TNK::Literal:
      return T.string();  // string literal types are treated as 'string'
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
        if (a->state == 1) return errT(l, "type alias '" + name + "' circularly references itself");
        if (!a->type) {
          a->state = 1;
          std::string savedFile = curFile;
          a->type = resolveType(a->decl->typeBody);
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
  if (name == "number") { noArgs(); return T.number(); }
  if (name == "string") { noArgs(); return T.string(); }
  if (name == "boolean") { noArgs(); return T.boolean(); }
  if (name == "bigint") { noArgs(); return T.bigint(); }
  if (name == "void") { noArgs(); return T.voidT(); }
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
    return T.array(resolveType(args[0]));
  }
  if (name == "Map" || name == "ReadonlyMap") {
    if (!nargs(2)) return T.unknown();
    return T.map(resolveType(args[0]), resolveType(args[1]));
  }
  if (name == "Set" || name == "ReadonlySet") {
    if (!nargs(1)) return T.unknown();
    return T.set(resolveType(args[0]));
  }
  if (name == "Uint8Array") { noArgs(); return T.bytes(); }
  if (name == "ArrayBuffer") { noArgs(); return T.arrayBuffer(); }
  if (name == "DataView") { noArgs(); return T.dataView(); }
  if (name == "KeyHandle" || name == "crypto.KeyHandle") { noArgs(); return T.handle(); }
  if (name == "TextEncoder") { noArgs(); return T.textEncoder(); }
  if (name == "TextDecoder") { noArgs(); return T.textDecoder(); }
  if (name == "Error") { noArgs(); return T.error(); }
  if (name == "Date") { noArgs(); return T.date(); }
  if (name == "Record" || name == "Partial" || name == "Required" || name == "Pick" || name == "Omit" || name == "Readonly")
    return errT(l, "utility type '" + name + "' is not supported; use Map<K, V> or an interface");
  if (name == "Promise") return errT(l, "Promise is not supported (no async in E7 v1)");
  if (name == "Int32Array" || name == "Uint32Array" || name == "Float64Array" || name == "Uint16Array" ||
      name == "Int8Array" || name == "Int16Array" || name == "Float32Array" || name == "Uint8ClampedArray")
    return errT(l, "'" + name + "' is not supported; use Uint8Array, DataView or typed arrays like u32[]");
  return errT(l, "unknown type '" + name + "'" + suggestName(name, true));
}

void Checker::resolveInterface(InterfaceSym* is) {
  if (is->state == 2) return;
  if (is->state == 1) {
    if (is->inExtends) err(is->loc, "interface '" + is->name + "' circularly extends itself");
    else
      err(is->loc, "interface '" + is->name + "' refers to itself; recursive interfaces are not supported yet. "
                   "Use a class for linked or tree-shaped data, e.g. class " + is->name + " { next: " + is->name +
                   " | null = null; ... }");
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
  for (auto& m : is->decl->typeBody->members) {
    Type* pt = resolveType(m.type);
    if (sh->findProp(m.name) || sh->findMethod(m.name)) {
      // allow identical redeclaration
    }
    if (m.method) sh->methods.push_back({m.accessor == 1 ? "get " + m.name : m.accessor == 2 ? "set " + m.name : m.name, pt});
    else {
      if (m.optional) pt = T.optional(pt, false, true);
      sh->props.push_back({m.name, pt, m.optional});
    }
  }
  scope = savedScope;
  is->state = 2;
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
      Expr* x = m.init;
      bool neg = false;
      if (x->k == EK::Unary && x->raw == "-" && x->a->k == EK::Num) { neg = true; x = x->a; }
      if (x->k == EK::Num) {
        mem.num = neg ? -x->num : x->num;
        next = mem.num + 1;
        anyNum = true;
      } else if (x->k == EK::Str) {
        mem.str = x->str;
        anyStr = true;
      } else {
        err(m.init->loc, "enum member initializers must be number or string literals");
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
  if (d->extends && !holder) {
    Type* pt = resolveType(d->extends);
    if (pt->t != TT::Class) err(d->extends->loc, "a class can only extend another class");
    else if (pt->cls->state == 1 || pt->cls == c) err(d->extends->loc, "class '" + c->name + "' circularly extends itself");
    else {
      resolveClass(pt->cls);
      c->parent = pt->cls;
      c->fields = pt->cls->fields;
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
  auto addField = [&](const std::string& name, Type* t, Expr* init, bool optional, Loc l, bool readonly) {
    if (name[0] == '#' && c->fieldIdx.count(name) && c->fields[c->fieldIdx[name]].owner != c) {
      err(l, "private field '" + name + "' is also declared by a base class; e7 needs distinct private names in a class hierarchy");
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
    c->fields.push_back({name, t, init, optional, c, l, readonly});
  };
  // parameter properties come first (TS emits them first in the constructor)
  if (ctorDecl) {
    for (auto& p : ctorDecl->params) {
      if (!p.isProperty) continue;
      if (!p.type) { err(p.loc, "parameter property '" + p.name + "' needs a type annotation"); continue; }
      Type* t = resolveType(p.type);
      if (p.optional) t = T.optional(t, false, true);
      addField(p.name, t, nullptr, p.optional, p.loc, p.readonly);
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
    if (m.accessor && m.name[0] == '#') { err(m.loc, "private accessors (get #x / set #x) are not supported; use a private field or method"); continue; }
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
  for (auto& m : d->members) {
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
      static std::vector<std::unique_ptr<VarDecl>> keep;
      keep.emplace_back(vd);
      v->decl = vd;
      if (c->staticFields.count(m.name) || c->staticMethods.count(m.name) || c->staticMethods.count("get " + m.name) ||
          c->staticMethods.count("set " + m.name))
        err(m.loc, "duplicate static member '" + m.name + "'");
      c->staticFields[m.name] = v;
      continue;
    }
    Type* t = nullptr;
    if (m.type) {
      t = resolveType(m.type);
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
    addField(m.name, t, m.init, m.optional, m.loc, m.readonly);
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
      if (base->type && ov->type && base->type != ov->type && ov->type->ret && base->type->ret)
        err(ov->loc, "method '" + ov->name + "' must have the same signature as the overridden method (" +
                         typeName(base->type) + ")");
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
  // implements: structural check
  for (Type* it : c->implements) {
    if (!structurallyAssignable(c->type, it)) {
      std::string why;
      for (auto& p : it->shape->props) {
        if (c->fieldIdx.count(p.name) || c->abstractProp(p.name)) continue;
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
        Scope* s0 = scope;
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
      if (p.init) f->hasDefaults = true;
      if (p.optional && p.init) err(p.loc, "parameter cannot be optional and have an initializer");
      if (p.optional && t) t = T.optional(t, false, true);
      if (isOpt) seenOptional = true;
      else if (seenOptional && !p.rest) err(p.loc, "a required parameter cannot follow an optional parameter");
      ps.push_back(t);
      opt.push_back(isOpt);
    }
  }
  Type* ret = f->retNode ? resolveType(f->retNode) : nullptr;
  if (f->isCtor || f->accessor == 2) ret = T.voidT();
  if (f->isNative) {
    if (!ret) ret = T.voidT();
    for (Type* t : ps)
      if (t && t->t != TT::Number && t->t != TT::Bool && t->t != TT::String && t->t != TT::Int && t->t != TT::Bytes &&
          t->t != TT::Handle)
        err(f->loc, "native function parameters must be number, boolean, string, integer, Uint8Array or KeyHandle");
    if (ret->t != TT::Void && ret->t != TT::Number && ret->t != TT::Bool && ret->t != TT::String && ret->t != TT::Int &&
        ret->t != TT::Bytes)
      err(f->loc, "native function return type must be void, number, boolean, string, integer or Uint8Array");
  }
  std::vector<Type*> pp;
  for (Type* t : ps) pp.push_back(t ? t : T.unknown());
  if (!ret) { f->inferRet = true; ret = T.unknown(); }
  f->type = T.func(pp, ret, opt, rest);
  curFile = savedFile;
  scope = savedSigScope;
}

}  // namespace e7
