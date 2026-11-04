#include <algorithm>
#include "checker.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace e7 {

Checker::Checker(Program& p, std::function<SourceFile*(const std::string&, const std::string&)> resolve)
    : P(p), T(p.types), resolveModule(std::move(resolve)) {}

void Checker::err(Loc l, const std::string& msg) {
  for (auto& e : P.errors)
    if (e.file == curFile && e.loc.line == l.line && e.loc.col == l.col) return;  // one error per location
  P.errors.push_back(CompileError{curFile, l, msg});
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
        if (!s->typeParams.empty()) err(s->loc, "generic functions are not supported yet");
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
        if (!s->typeParams.empty()) err(s->loc, "generic classes are not supported yet");
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
        if (!s->typeParams.empty()) err(s->loc, "generic interfaces are not supported yet");
        InterfaceSym* is = P.make<InterfaceSym>();
        is->name = s->name;
        is->decl = s;
        declare(s->name, is, s->loc);
        if (s->exported) fi->exports[s->name] = is;
        break;
      }
      case SK::TypeAlias: {
        if (!s->typeParams.empty()) err(s->loc, "generic type aliases are not supported yet");
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
    if (s->from != "e7:crypto" && s->from != "e7:encoding") {
      err(s->loc, "unknown builtin module '" + s->from + "'");
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
      err(s->loc, "module '" + s->from + "' has no exported member '" + sp.name + "'");
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
        if (m.method) sh->methods.push_back({m.name, pt});
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
    return errT(l, "type '" + name + "' is not supported in E7 (all types must be static)");
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
  return errT(l, "unknown type '" + name + "'");
}

void Checker::resolveInterface(InterfaceSym* is) {
  if (is->state == 2) return;
  if (is->state == 1) { err(is->loc, "interface '" + is->name + "' circularly extends itself"); return; }
  is->state = 1;
  T.shapes.emplace_back(new ObjShape());
  ObjShape* sh = T.shapes.back().get();
  sh->name = is->name;
  is->type = T.objectType(sh);
  for (TypeNode* ext : is->decl->ifaceExtends) {
    Type* pt = resolveType(ext);
    if (pt->t != TT::Object) { err(ext->loc, "an interface can only extend interfaces"); continue; }
    for (auto& p : pt->shape->props) sh->props.push_back(p);
    for (auto& m : pt->shape->methods) sh->methods.push_back(m);
  }
  for (auto& m : is->decl->typeBody->members) {
    Type* pt = resolveType(m.type);
    if (sh->findProp(m.name) || sh->findMethod(m.name)) {
      // allow identical redeclaration
    }
    if (m.method) sh->methods.push_back({m.name, pt});
    else {
      if (m.optional) pt = T.optional(pt, false, true);
      sh->props.push_back({m.name, pt, m.optional});
    }
  }
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
  if (d->extends) {
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
  for (TypeNode* im : d->implements) {
    Type* it = resolveType(im);
    if (it->t != TT::Object) err(im->loc, "a class can only implement interfaces");
    else c->implements.push_back(it);
  }
  // constructor symbol (explicit or synthesized)
  ClassMember* ctorDecl = nullptr;
  for (auto& m : d->members)
    if (m.kind == ClassMember::Ctor) {
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
  if (!ctorDecl && c->parent) {
    // implicit `constructor(...args) { super(...args); }`
    resolveSig(c->parent->ctor);
    ctor->inheritCtor = true;
    ctor->sigResolved = true;
    ctor->hasDefaults = c->parent->ctor->hasDefaults;
    ctor->type = c->parent->ctor->type;
  }

  auto addField = [&](const std::string& name, Type* t, Expr* init, bool optional, Loc l) {
    if (c->fieldIdx.count(name) || c->methodIdx.count(name)) {
      err(l, "duplicate member '" + name + "' in class '" + c->name + "'");
      return;
    }
    c->fieldIdx[name] = (int)c->fields.size();
    c->fields.push_back({name, t, init, optional, c, l});
  };
  // parameter properties come first (TS emits them first in the constructor)
  if (ctorDecl) {
    for (auto& p : ctorDecl->params) {
      if (!p.isProperty) continue;
      if (!p.type) { err(p.loc, "parameter property '" + p.name + "' needs a type annotation"); continue; }
      Type* t = resolveType(p.type);
      if (p.optional) t = T.optional(t, false, true);
      addField(p.name, t, nullptr, p.optional, p.loc);
    }
  }
  // methods
  for (auto& m : d->members) {
    if (m.kind != ClassMember::Method) continue;
    curFn = nullptr;
    FuncSym* f = newFunc(m.name, &m.params, m.retType, m.body, nullptr);
    f->parent = nullptr;
    f->cls = c;
    f->isTopLevel = true;
    f->qualName = c->name + "." + m.name;
    f->isStatic = m.isStatic;
    m.fn = f;
    if (m.isStatic) {
      if (c->staticMethods.count(m.name) || c->staticFields.count(m.name)) err(m.loc, "duplicate static member '" + m.name + "'");
      c->staticMethods[m.name] = f;
      continue;
    }
    f->isMethod = true;
    f->thisVar = newVar("this", c->type, true, m.loc);
    f->thisVar->isThis = true;
    f->thisVar->owner = f;
    if (c->fieldIdx.count(m.name)) { err(m.loc, "member '" + m.name + "' conflicts with a field"); continue; }
    auto it = c->methodIdx.find(m.name);
    if (it != c->methodIdx.end()) {
      if (c->vtable[it->second].fn->cls == c) { err(m.loc, "duplicate method '" + m.name + "'"); continue; }
      c->vtable[it->second].fn = f;  // override, signature checked below
    } else {
      c->methodIdx[m.name] = (int)c->vtable.size();
      c->vtable.push_back({m.name, f});
    }
  }
  for (auto& m : d->members) {
    if (m.kind != ClassMember::Field) continue;
    if (m.isStatic) {
      VarSym* v = newVar(c->name + "." + m.name, nullptr, false, m.loc);
      v->isGlobal = true;
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
      if (c->staticFields.count(m.name)) err(m.loc, "duplicate static member '" + m.name + "'");
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
    addField(m.name, t, m.init, m.optional, m.loc);
  }
  scope = savedScope;
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
  // implements: structural check
  for (Type* it : c->implements) {
    if (!structurallyAssignable(c->type, it))
      err(d->loc, "class '" + c->name + "' does not correctly implement '" + typeName(it) + "'");
  }
  curFile = savedFile;
}

void Checker::resolveSig(FuncSym* f) {
  if (!f || f->sigResolved) return;
  f->sigResolved = true;
  std::string savedFile = curFile;
  curFile = f->file.empty() ? curFile : f->file;
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
  if (f->isCtor) ret = T.voidT();
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
}

}  // namespace e7
