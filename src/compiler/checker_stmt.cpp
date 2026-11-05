// Checker: relations, functions and statements.
#include <algorithm>

#include "checker.h"

namespace e7 {

// ------------------------------------------------------------------ relations
bool Checker::structurallyAssignable(Type* src, Type* dst) {
  if (dst->t != TT::Object) return false;
  ObjShape* ds = dst->shape;
  if (src->t == TT::Class) {
    ClassSym* c = src->cls;
    resolveClass(c);
    for (auto& p : ds->props) {
      auto it = c->fieldIdx.find(p.name);
      if (it == c->fieldIdx.end()) return false;
      Type* ft = c->fields[it->second].type;
      if (ft != p.type && !(kindOf(ft) == K_R && kindOf(p.type) == K_R && !isBoxedOptional(ft) && !isBoxedOptional(p.type) && assignable(ft, p.type)))
        return false;
    }
    for (auto& m : ds->methods) {
      auto it = c->methodIdx.find(m.name);
      if (it == c->methodIdx.end()) return false;
      FuncSym* f = c->vtable[it->second].fn;
      resolveSig(f);
      if (f->inferRet && !f->checked) checkFunction(f);
      if (f->type != m.fn) return false;
    }
    return true;
  }
  if (src->t == TT::Object) {
    ObjShape* ss = src->shape;
    for (auto& p : ds->props) {
      const ObjShape::Prop* sp = ss->findProp(p.name);
      if (!sp) return false;
      if (sp->type != p.type && !(kindOf(sp->type) == K_R && kindOf(p.type) == K_R && !isBoxedOptional(sp->type) &&
                                  !isBoxedOptional(p.type) && assignable(sp->type, p.type)))
        return false;
    }
    for (auto& m : ds->methods) {
      const ObjShape::Method* sm = ss->findMethod(m.name);
      if (!sm || sm->fn != m.fn) return false;
    }
    return true;
  }
  return false;
}

static bool isAncestor(ClassSym* anc, ClassSym* c) {
  for (; c; c = c->parent)
    if (c == anc) return true;
  return false;
}

bool Checker::assignable(Type* src, Type* dst) {
  if (src == dst) return true;
  if (src->t == TT::Unknown || dst->t == TT::Unknown || src->t == TT::Never) return true;
  switch (dst->t) {
    case TT::Optional: {
      if (src->t == TT::Null) return dst->hasNull;
      if (src->t == TT::Undefined) return dst->hasUndef;
      if (src->t == TT::Optional) {
        if (src->hasNull && !dst->hasNull) return false;
        if (src->hasUndef && !dst->hasUndef) return false;
        if (kindOf(src->elem) != K_R || kindOf(dst->elem) != K_R) return src->elem == dst->elem;
        return assignable(src->elem, dst->elem);
      }
      if (kindOf(src) != K_R || kindOf(dst->elem) != K_R) return src == dst->elem;
      return assignable(src, dst->elem);
    }
    case TT::Class:
      return src->t == TT::Class && isAncestor(dst->cls, src->cls);
    case TT::Array:
      // arrays of references are covariant (as in TypeScript); representation is identical
      return src->t == TT::Array && kindOf(src->elem) == K_R && kindOf(dst->elem) == K_R &&
             !isBoxedOptional(src->elem) && !isBoxedOptional(dst->elem) && src->elem->t != TT::Func &&
             dst->elem->t != TT::Func && assignable(src->elem, dst->elem);
    case TT::Object:
      return structurallyAssignable(src, dst);
    case TT::Number:
      return src->t == TT::Enum && !src->en->isString;
    case TT::String:
      return src->t == TT::Enum && src->en->isString;
    default:
      return false;
  }
}

Expr* Checker::wrapConv(Expr* e, ConvKind k, Type* t) {
  Expr* c = P.arena.expr(EK::Conv, e->loc);
  c->a = e;
  c->conv = k;
  c->type = t;
  return c;
}

bool Checker::coerce(Expr*& e, Type* target, const char* ctx) {
  Type* t = e->type;
  if (!t || !target) return true;
  if (t == target) return true;
  if (target->t == TT::Void && ctx && std::string(ctx) == "return") return false;
  if (!assignable(t, target)) {
    err(e->loc, "type '" + typeName(t) + "' is not assignable to type '" + typeName(target) + "'");
    return false;
  }
  if (target->t == TT::Optional && t->t != TT::Optional && kindOf(t) != K_R && t->t != TT::Never && t->t != TT::Unknown) {
    e = wrapConv(e, kindOf(t) == K_F ? ConvKind::NumToBox : ConvKind::IntToBox, target);
    return true;
  }
  // representation-preserving
  e = wrapConv(e, ConvKind::None, target);
  return true;
}

Type* Checker::widen(Type* t) { return t; }

Type* Checker::unify(Type* a, Type* b, Loc l, const char* what) {
  if (a == b) return a;
  if (a->t == TT::Never || a->t == TT::Unknown) return b;
  if (b->t == TT::Never || b->t == TT::Unknown) return a;
  if (a->t == TT::Null || a->t == TT::Undefined) return T.optional(b, a->t == TT::Null, a->t == TT::Undefined);
  if (b->t == TT::Null || b->t == TT::Undefined) return T.optional(a, b->t == TT::Null, b->t == TT::Undefined);
  if (a->t == TT::Optional || b->t == TT::Optional) {
    Type* ab = baseOf(a);
    Type* bb = baseOf(b);
    Type* u = unify(ab, bb, l, what);
    bool n = (a->t == TT::Optional && a->hasNull) || (b->t == TT::Optional && b->hasNull);
    bool un = (a->t == TT::Optional && a->hasUndef) || (b->t == TT::Optional && b->hasUndef);
    return T.optional(u, n, un);
  }
  if (a->t == TT::Class && b->t == TT::Class) {
    for (ClassSym* c = a->cls; c; c = c->parent)
      if (isAncestor(c, b->cls)) return c->type;
  }
  if (assignable(a, b)) return b;
  if (assignable(b, a)) return a;
  return errT(l, std::string("incompatible types in ") + what + ": '" + typeName(a) + "' and '" + typeName(b) + "'");
}

// ------------------------------------------------------------------ functions
void Checker::checkFunction(FuncSym* f) {
  if (f->checked || f->isNative) return;
  if (f->checking) return;
  f->checking = true;
  Scope* savedScope = scope;
  FuncSym* savedFn = curFn;
  std::string savedFile = curFile;
  auto savedLoops = loops;
  auto savedLabels = pendingLabels;
  loops.clear();
  pendingLabels.clear();
  if (!f->file.empty()) curFile = f->file;
  resolveSig(f);
  if (f->defScope) scope = f->defScope;
  else if (fileInfo.count(curFile)) scope = fileInfo[curFile]->scope;
  curFn = f;
  pushScope(true);
  checkBody(f);
  scope = savedScope;
  curFn = savedFn;
  curFile = savedFile;
  loops = savedLoops;
  pendingLabels = savedLabels;
  f->checking = false;
  f->checked = true;
}

static bool containsBreakFor(Stmt* s, const std::string& label, int depth);

void Checker::checkBody(FuncSym* f) {
  if (f->thisVar) {
    scope->names["this"] = f->thisVar;
  }
  // parameters
  f->paramSyms.clear();
  if (f->inheritCtor) {
    for (size_t i = 0; i < f->type->params.size(); i++) {
      VarSym* v = newVar("$arg" + std::to_string(i), f->type->params[i], false, f->loc);
      v->isParam = true;
      v->paramIndex = (int)i;
      v->owner = f;
      f->paramSyms.push_back(v);
    }
  }
  if (f->params) {
    for (size_t i = 0; i < f->params->size(); i++) {
      Param& p = (*f->params)[i];
      Type* pt = f->type->params[i];
      VarSym* v = newVar(p.name, pt, false, p.loc);
      v->isParam = true;
      v->paramIndex = (int)i;
      v->owner = f;
      p.sym = v;
      f->paramSyms.push_back(v);
      declare(p.name, v, p.loc);
      if (p.init) {
        // default values are evaluated in the callee when the argument is omitted
        Type* base = pt;
        check(p.init, base);
        coerce(p.init, base, "default value");
      }
    }
  }
  Type* declaredRet = f->inferRet ? nullptr : f->type->ret;
  f->returnTypes.clear();
  if (f->isCtor) {
    // field initializers are checked in constructor context
    ClassSym* c = f->cls;
    for (auto& fd : c->fields) {
      if (fd.owner != c || !fd.init) continue;
      if (!fd.init->type) check(fd.init, fd.type);
      coerce(fd.init, fd.type, "field initializer");
    }
    if (c->parent) {
      bool firstIsSuper = false;
      if (f->body && !f->body->stmts.empty()) {
        Stmt* s0 = f->body->stmts[0];
        if (s0->k == SK::Expr && s0->expr->k == EK::Call && s0->expr->a->k == EK::Super) firstIsSuper = true;
      }
      if (f->body && !firstIsSuper && !f->inheritCtor) err(f->body->loc, "derived class constructor must call super(...) as its first statement");
    }
  }
  if (f->exprBody) {
    Type* t = check(f->exprBody, declaredRet);
    if (declaredRet) {
      if (declaredRet->t != TT::Void) coerce(f->exprBody, declaredRet, "return");
    } else {
      f->returnTypes.push_back(t);
    }
  } else if (f->body) {
    checkBlockStmts(f->body->stmts);
  }
  if (f->inferRet) {
    Type* r = nullptr;
    for (Type* t : f->returnTypes) r = r ? unify(r, t, f->loc, "return statements") : t;
    if (!r) r = T.voidT();
    if (r->t == TT::Null || r->t == TT::Undefined) {
      err(f->loc, "cannot infer return type of '" + f->name + "' from null/undefined; add a return type annotation");
      r = T.unknown();
    }
    // a function returning values on some paths only
    if (r->t != TT::Void && f->body && !exits(f->body)) {
      err(f->loc, "not all code paths of '" + f->name + "' return a value");
    }
    f->type = T.func(f->type->params, r, f->type->optParams, f->type->restParam);
    f->inferRet = false;
    // coerce return expressions to the unified type
  } else if (declaredRet && declaredRet->t != TT::Void && declaredRet->t != TT::Never && f->body && !exits(f->body)) {
    bool allowUndef = declaredRet->t == TT::Optional && declaredRet->hasUndef;
    if (!allowUndef) err(f->loc, "function '" + f->name + "' lacks ending return statement");
  }
}

void Checker::hoistFunctions(std::vector<Stmt*>& stmts) {
  for (Stmt* s : stmts) {
    if (s->k != SK::Func || s->fn) continue;
    if (!s->typeParams.empty()) err(s->loc, "generic functions are not supported yet; write one function per concrete type (builtins such as Array<T>, Map<K, V> and Set<T> are generic)");
    FuncSym* f = newFunc(s->name, &s->params, s->retType, s->body, nullptr);
    f->declStmt = s;
    f->parent = curFn;
    f->defScope = scope;
    f->qualName = (curFn && !curFn->isInit ? curFn->qualName + "." : "") + s->name;
    s->fn = f;
    VarSym* sv = newVar(s->name, nullptr, true, s->loc);
    sv->owner = curFn;
    f->selfVar = sv;
    declare(s->name, f, s->loc);
    resolveSig(f);
    sv->type = f->type;
  }
}

void Checker::checkBlockStmts(std::vector<Stmt*>& stmts) {
  hoistFunctions(stmts);
  for (Stmt* s : stmts) checkStmt(s);
}

bool Checker::exits(Stmt* s) {
  if (!s) return false;
  switch (s->k) {
    case SK::Return: case SK::Throw: case SK::Break: case SK::Continue: return true;
    case SK::Block:
      for (Stmt* x : s->stmts) if (exits(x)) return true;
      return false;
    case SK::If: return s->els && exits(s->then) && exits(s->els);
    case SK::Try: return exits(s->body) && exits(s->els);
    case SK::While: {
      bool infinite = s->expr->k == EK::Bool && s->expr->bval;
      return infinite && !containsBreakFor(s->body, "", 0);
    }
    case SK::For: {
      bool infinite = !s->expr || (s->expr->k == EK::Bool && s->expr->bval);
      return infinite && !containsBreakFor(s->body, "", 0);
    }
    case SK::DoWhile: return exits(s->body) && !containsBreakFor(s->body, "", 0);
    case SK::Labeled: {
      Stmt* b = s->body;
      if (b->k == SK::While || b->k == SK::For) {
        bool infinite = b->k == SK::While ? (b->expr->k == EK::Bool && b->expr->bval) : (!b->expr || (b->expr->k == EK::Bool && b->expr->bval));
        return infinite && !containsBreakFor(b->body, "", 0) && !containsBreakFor(b->body, s->label, 0);
      }
      return exits(b) && !containsBreakFor(b, s->label, 0);
    }
    case SK::Switch: {
      // exits if there is a default and every case ends by exiting (no breaks)
      bool hasDefault = false;
      for (auto& c : s->cases) if (!c.test) hasDefault = true;
      if (!hasDefault) return false;
      for (size_t i = 0; i < s->cases.size(); i++) {
        auto& c = s->cases[i];
        for (Stmt* x : c.body) if (containsBreakFor(x, "", 0)) return false;
      }
      if (s->cases.empty()) return false;
      auto& last = s->cases.back();
      for (Stmt* x : last.body) if (exits(x)) return true;
      return false;
    }
    default: return false;
  }
}

static bool containsBreakFor(Stmt* s, const std::string& label, int depth) {
  if (!s) return false;
  switch (s->k) {
    case SK::Break:
      if (label.empty()) return s->label.empty() && depth == 0;
      return s->label == label;
    case SK::Block:
      for (Stmt* x : s->stmts) if (containsBreakFor(x, label, depth)) return true;
      return false;
    case SK::If: return containsBreakFor(s->then, label, depth) || containsBreakFor(s->els, label, depth);
    case SK::Try: return containsBreakFor(s->body, label, depth) || containsBreakFor(s->els, label, depth);
    case SK::While: case SK::DoWhile: case SK::For: case SK::ForOf:
      return containsBreakFor(s->body, label, depth + 1);
    case SK::Switch:
      for (auto& c : s->cases)
        for (Stmt* x : c.body) if (containsBreakFor(x, label, depth + 1)) return true;
      return false;
    case SK::Labeled: return containsBreakFor(s->body, label, depth);
    default: return false;
  }
}

void Checker::collectAssignedExpr(Expr* e, std::set<std::string>& out) {
  if (!e) return;
  if ((e->k == EK::Assign || e->k == EK::Update) && e->a) {
    Expr* t = e->a;
    while (t->k == EK::Paren) t = t->a;
    std::string path;
    Expr* x = t;
    while (x->k == EK::Member) { path = "." + x->raw + path; x = x->a; }
    if (x->k == EK::Ident) out.insert(x->raw + path);
    else if (x->k == EK::This) out.insert("this" + path);
    if (t->k == EK::Array)
      for (Expr* x : t->list) if (x->k == EK::Ident) out.insert(x->raw);
  }
  collectAssignedExpr(e->a, out);
  collectAssignedExpr(e->b, out);
  collectAssignedExpr(e->c, out);
  for (Expr* x : e->list) collectAssignedExpr(x, out);
  for (auto& p : e->props) collectAssignedExpr(p.value, out);
  if (e->exprBody) collectAssignedExpr(e->exprBody, out);
  if (e->body) collectAssigned(e->body, out);
}

void Checker::collectAssigned(Stmt* s, std::set<std::string>& out) {
  if (!s) return;
  collectAssignedExpr(s->expr, out);
  collectAssignedExpr(s->update, out);
  for (auto& d : s->decls) collectAssignedExpr(d.init, out);
  collectAssigned(s->then, out);
  collectAssigned(s->els, out);
  collectAssigned(s->body, out);
  collectAssigned(s->init, out);
  for (Stmt* x : s->stmts) collectAssigned(x, out);
  for (auto& c : s->cases) {
    collectAssignedExpr(c.test, out);
    for (Stmt* x : c.body) collectAssigned(x, out);
  }
}

void Checker::invalidateAssigned(Stmt* body, Expr* extra) {
  std::set<std::string> names;
  collectAssigned(body, names);
  collectAssignedExpr(extra, names);
  for (auto& n : names) {
    // n is "root" or "root.a.b"
    std::string root = n.substr(0, n.find('.'));
    Sym* s = lookup(root);
    if (!s || s->k != SymK::Var) continue;
    std::string key = varKey(static_cast<VarSym*>(s));
    if (n.size() > root.size()) key += n.substr(root.size());
    invalidatePath(key);
  }
}

// ------------------------------------------------------------------ variables
Type* Checker::globalVarType(VarSym* v) {
  if (v->type || v->checked) return v->type ? v->type : T.unknown();
  if (v->typing) {
    return errT(v->loc, "'" + v->name + "' is used in its own initializer; add a type annotation");
  }
  if (!v->decl) return T.unknown();
  // type lazily: annotation or initializer checked in init context
  v->typing = true;
  Scope* s0 = scope;
  FuncSym* f0 = curFn;
  std::string file0 = curFile;
  auto loops0 = loops;
  loops.clear();
  VarDecl& d = *v->decl;
  if (d.type) {
    v->type = resolveType(d.type);
  } else if (d.init) {
    // find the file scope where the variable is declared
    for (auto& fi : fileInfos)
      for (auto& kv : fi->scope->names)
        if (kv.second == v) { scope = fi->scope; curFile = fi->file->path; }
    curFn = P.init;
    Type* t = check(d.init, nullptr);
    if (t->t == TT::Null || t->t == TT::Undefined) {
      err(d.loc, "cannot infer type of '" + v->name + "' from null/undefined; add a type annotation");
      t = T.unknown();
    }
    if (t->t == TT::Void || t->t == TT::Never) {
      err(d.loc, "an expression of type 'void' cannot be assigned to a variable");
      t = T.unknown();
    }
    v->type = t;
    d.sym = v;
    v->checked = true;  // initializer already checked
  } else {
    err(d.loc, "'" + v->name + "' needs a type annotation or initializer");
    v->type = T.unknown();
  }
  scope = s0;
  curFn = f0;
  curFile = file0;
  loops = loops0;
  v->typing = false;
  return v->type;
}

void Checker::bindPattern(VarDecl& d, Type* t, bool isConst, bool topLevel, Loc l) {
  if (!d.isArrayPat && !d.isObjPat) {
    if (d.name.empty()) return;  // hole
    VarSym* v = newVar(d.name, t, isConst, d.loc);
    v->owner = curFn;
    if (topLevel) {
      v->isGlobal = true;
      v->owner = nullptr;
      v->globalIdx = (int)P.globals.size();
      P.globals.push_back(v);
    }
    d.sym = v;
    declare(d.name, v, d.loc);
    return;
  }
  if (d.isArrayPat) {
    for (size_t i = 0; i < d.elems.size(); i++) {
      Type* et;
      if (t->t == TT::Array) et = t->elem;
      else if (t->t == TT::Tuple) {
        if (i >= t->elems.size()) { err(d.loc, "tuple has no element at index " + std::to_string(i)); et = T.unknown(); }
        else et = t->elems[i];
      } else if (t->t == TT::Unknown) et = T.unknown();
      else { err(l, "cannot destructure a value of type '" + typeName(t) + "' as an array"); et = T.unknown(); }
      bindPattern(d.elems[i], et, isConst, topLevel, l);
    }
    return;
  }
  // object pattern
  for (size_t i = 0; i < d.elems.size(); i++) {
    const std::string& key = d.keys[i];
    Type* ft = T.unknown();
    if (t->t == TT::Class) {
      auto it = t->cls->fieldIdx.find(key);
      if (it == t->cls->fieldIdx.end()) err(d.loc, "property '" + key + "' does not exist on type '" + typeName(t) + "'");
      else ft = t->cls->fields[it->second].type;
    } else if (t->t == TT::Object) {
      const ObjShape::Prop* p = t->shape->findProp(key);
      if (!p) err(d.loc, "property '" + key + "' does not exist on type '" + typeName(t) + "'");
      else ft = p->type;
    } else if (t->t != TT::Unknown) {
      err(l, "cannot destructure a value of type '" + typeName(t) + "'");
    }
    bindPattern(d.elems[i], ft, isConst, topLevel, l);
  }
}

void Checker::checkVarDecl(Stmt* s, VarDecl& d, bool isConst, bool topLevel) {
  bool simple = !d.isArrayPat && !d.isObjPat;
  if (simple && topLevel && d.sym) {
    VarSym* v = d.sym;
    if (v->checked) {
      if (v->type && d.init && d.init->type) coerce(d.init, v->type);
      return;
    }
    Type* declared = d.type ? resolveType(d.type) : nullptr;
    if (d.init) {
      Type* t = check(d.init, declared);
      if (declared) coerce(d.init, declared);
      else {
        if (t->t == TT::Null || t->t == TT::Undefined) {
          err(d.loc, "cannot infer type of '" + d.name + "' from null/undefined; add a type annotation");
          t = T.unknown();
        }
        if (t->t == TT::Void || t->t == TT::Never) {
          err(d.loc, "an expression of type 'void' cannot be assigned to a variable");
          t = T.unknown();
        }
        declared = t;
      }
    } else if (!declared) {
      err(d.loc, "'" + d.name + "' needs a type annotation or initializer");
      declared = T.unknown();
    } else if (isConst) {
      err(d.loc, "const declarations must be initialized");
    }
    v->type = declared;
    v->checked = true;
    return;
  }
  Type* declared = d.type ? resolveType(d.type) : nullptr;
  Type* t = declared;
  if (d.init) {
    Type* it = check(d.init, declared);
    if (declared) coerce(d.init, declared);
    else {
      if (it->t == TT::Null || it->t == TT::Undefined) {
        err(d.loc, "cannot infer variable type from null/undefined; add a type annotation");
        it = T.unknown();
      }
      if (it->t == TT::Void) {
        err(d.loc, "an expression of type 'void' cannot be assigned to a variable");
        it = T.unknown();
      }
      t = it;
    }
  } else {
    if (!declared) {
      err(d.loc, "variable needs a type annotation or initializer");
      t = T.unknown();
    } else if (isConst) {
      err(d.loc, "const declarations must be initialized");
    }
    if (!simple) err(d.loc, "destructuring declarations must be initialized");
  }
  bindPattern(d, t, isConst, topLevel && !simple ? true : false, d.loc);
  (void)s;
}

// ------------------------------------------------------------------ statements
void Checker::checkStmt(Stmt* s) {
  bool topLevel = curFn && curFn->isInit && scope && scope->parent == globalScope;
  switch (s->k) {
    case SK::Var:
      for (auto& d : s->decls) checkVarDecl(s, d, s->isConst, topLevel);
      break;
    case SK::Func:
      if (topLevel) break;  // top-level functions are checked separately
      if (!s->fn) { std::vector<Stmt*> one{s}; hoistFunctions(one); }
      checkFunction(s->fn);
      if (s->fn->selfVar) s->fn->selfVar->type = s->fn->type;
      break;
    case SK::Declare:
      if (!topLevel) err(s->loc, "'declare function' is only allowed at top level");
      break;
    case SK::Class:
      if (!topLevel) { err(s->loc, "classes must be declared at top level"); break; }
      if (s->cls) {
        resolveClass(s->cls);
        // static field initializers run here, in source order
        for (auto& kv : s->cls->staticFields) {
          VarSym* v = kv.second;
          VarDecl* d = v->decl;
          if (d && d->init) {
            Type* t = check(d->init, v->type);
            if (v->type) coerce(d->init, v->type);
            else v->type = t;
          }
          if (!v->type) { err(v->loc, "static field '" + kv.first + "' needs a type annotation or initializer"); v->type = T.unknown(); }
          v->checked = true;
        }
      }
      break;
    case SK::Interface: case SK::TypeAlias: case SK::Enum:
      if (!topLevel) err(s->loc, "type declarations must be at top level");
      break;
    case SK::Import: case SK::Export: case SK::Empty: case SK::Debugger:
      break;
    case SK::Expr: {
      Type* t = check(s->expr, nullptr);
      (void)t;
      break;
    }
    case SK::Block:
      pushScope();
      checkBlockStmts(s->stmts);
      popScope();
      break;
    case SK::If: {
      checkCond(s->expr);
      std::vector<Fact> tf, ff;
      factsFrom(s->expr, true, tf);
      factsFrom(s->expr, false, ff);
      pushScope();
      applyFacts(tf);
      checkStmt(s->then);
      popScope();
      if (s->els) {
        pushScope();
        applyFacts(ff);
        checkStmt(s->els);
        popScope();
      }
      bool te = exits(s->then), ee = s->els && exits(s->els);
      if (te && !ee) applyFacts(ff);
      else if (ee && !te) applyFacts(tf);
      break;
    }
    case SK::While: {
      invalidateAssigned(s->body, s->expr);
      checkCond(s->expr);
      std::vector<Fact> tf;
      factsFrom(s->expr, true, tf);
      pushScope();
      applyFacts(tf);
      loops.push_back({pendingLabels.empty() ? "" : pendingLabels.back(), true, false});
      pendingLabels.clear();
      checkStmt(s->body);
      loops.pop_back();
      popScope();
      break;
    }
    case SK::DoWhile: {
      invalidateAssigned(s->body, s->expr);
      loops.push_back({pendingLabels.empty() ? "" : pendingLabels.back(), true, false});
      pendingLabels.clear();
      pushScope();
      checkStmt(s->body);
      popScope();
      loops.pop_back();
      checkCond(s->expr);
      break;
    }
    case SK::For: {
      pushScope();
      std::string label = pendingLabels.empty() ? "" : pendingLabels.back();
      pendingLabels.clear();
      if (s->init) {
        if (s->init->k == SK::Var)
          for (auto& d : s->init->decls) checkVarDecl(s->init, d, s->init->isConst, false);
        else checkStmt(s->init);
      }
      invalidateAssigned(s->body, s->update);
      std::vector<Fact> tf;
      if (s->expr) {
        checkCond(s->expr);
        factsFrom(s->expr, true, tf);
      }
      pushScope();
      applyFacts(tf);
      loops.push_back({label, true, false});
      checkStmt(s->body);
      loops.pop_back();
      if (s->update) check(s->update, nullptr);
      popScope();
      popScope();
      break;
    }
    case SK::ForOf: {
      std::string label = pendingLabels.empty() ? "" : pendingLabels.back();
      pendingLabels.clear();
      Type* et = nullptr;
      s->declareKind = 0;
      {
        Expr* ie = s->expr;
        while (ie->k == EK::Paren) ie = ie->a;
        if (ie->k == EK::Call && ie->list.empty() && ie->a->k == EK::Member &&
            (ie->a->raw == "entries" || ie->a->raw == "keys" || ie->a->raw == "values")) {
          Type* ot = check(ie->a->a, nullptr);
          const std::string& mn = ie->a->raw;
          if (ot->t == TT::Array) {
            s->declareKind = mn == "entries" ? 1 : mn == "keys" ? 2 : 3;
            et = mn == "entries" ? T.tuple({T.number(), ot->elem}) : mn == "keys" ? T.number() : ot->elem;
          } else if (ot->t == TT::Map) {
            s->declareKind = mn == "keys" ? 4 : mn == "values" ? 5 : 6;
            et = mn == "keys" ? ot->key : mn == "values" ? ot->val : T.tuple({ot->key, ot->val});
          } else if (ot->t == TT::Set) {
            s->declareKind = 7;
            et = mn == "entries" ? T.tuple({ot->elem, ot->elem}) : ot->elem;
            if (mn == "entries") s->declareKind = 8;
          }
          if (s->declareKind) s->expr = ie->a->a;
        }
      }
      if (!et) {
        Type* it = check(s->expr, nullptr);
        et = elemTypeForIter(s->expr, it, s->expr->loc);
      }
      pushScope();
      VarDecl& d = s->decls[0];
      // [k, v] of map: bind directly without allocating tuples
      bindPattern(d, et, s->isConst, false, d.loc);
      invalidateAssigned(s->body);
      loops.push_back({label, true, false});
      pushScope();
      checkStmt(s->body);
      popScope();
      loops.pop_back();
      popScope();
      break;
    }
    case SK::Return: {
      if (!curFn || curFn->isInit) { err(s->loc, "'return' outside of a function"); break; }
      FuncSym* f = curFn;
      Type* declared = f->inferRet ? nullptr : f->type->ret;
      if (s->expr) {
        Type* t = check(s->expr, declared);
        if (declared) {
          if (declared->t == TT::Void) err(s->loc, "a void function cannot return a value");
          else coerce(s->expr, declared, "return");
        } else {
          f->returnTypes.push_back(t);
        }
      } else {
        if (declared && declared->t != TT::Void && !(declared->t == TT::Optional && declared->hasUndef))
          err(s->loc, "function must return a value of type '" + typeName(declared) + "'");
        if (!declared) f->returnTypes.push_back(T.voidT());
      }
      break;
    }
    case SK::Break: case SK::Continue: {
      bool isBreak = s->k == SK::Break;
      bool ok = false;
      for (auto it = loops.rbegin(); it != loops.rend(); ++it) {
        if (!s->label.empty()) {
          if (it->label == s->label) { ok = isBreak || it->isLoop; break; }
        } else if (it->isLoop || (isBreak && it->isSwitch)) {
          ok = true;
          break;
        }
      }
      if (!ok) err(s->loc, s->label.empty() ? (isBreak ? "'break' outside of a loop or switch" : "'continue' outside of a loop")
                                            : "unknown label '" + s->label + "'");
      break;
    }
    case SK::Labeled: {
      bool isLoop = s->body->k == SK::While || s->body->k == SK::DoWhile || s->body->k == SK::For || s->body->k == SK::ForOf;
      if (isLoop) {
        pendingLabels.push_back(s->label);
        checkStmt(s->body);
        pendingLabels.clear();
      } else {
        loops.push_back({s->label, false, false});
        checkStmt(s->body);
        loops.pop_back();
      }
      break;
    }
    case SK::Switch: {
      Type* dt = check(s->expr, nullptr);
      if (dt->t != TT::Number && dt->t != TT::String && dt->t != TT::Int && dt->t != TT::Enum && dt->t != TT::Bool &&
          dt->t != TT::Unknown)
        err(s->expr->loc, "switch requires a number, string, boolean, integer or enum value");
      pushScope();
      loops.push_back({pendingLabels.empty() ? "" : pendingLabels.back(), false, true});
      pendingLabels.clear();
      int defaults = 0;
      std::vector<Stmt*> all;
      for (auto& c : s->cases)
        for (Stmt* x : c.body) all.push_back(x);
      hoistFunctions(all);
      for (auto& c : s->cases) {
        if (c.test) {
          Type* ct = check(c.test, dt);
          if (ct != dt && !(dt->t == TT::Unknown || ct->t == TT::Unknown)) {
            if (!assignable(ct, dt)) err(c.test->loc, "case type '" + typeName(ct) + "' is not comparable to '" + typeName(dt) + "'");
          }
        } else {
          defaults++;
        }
        for (Stmt* x : c.body) checkStmt(x);
      }
      if (defaults > 1) err(s->loc, "multiple default clauses");
      loops.pop_back();
      popScope();
      break;
    }
    case SK::Throw: {
      // throw an Error (new Error / TypeError / RangeError / SyntaxError(message), an Error value, or a
      // caught error to rethrow it); try/catch can catch it
      Type* t = check(s->expr, nullptr);
      if (t->t != TT::Error && t->t != TT::Caught && t->t != TT::Unknown) {
        if (t->t == TT::String)
          err(s->expr->loc, "throwing a string is not supported; throw new Error(message) instead");
        else
          err(s->expr->loc, "only Error objects can be thrown (throw new Error(message)); got '" + typeName(t) + "'");
      }
      break;
    }
    case SK::Try: {
      // try { } catch (e) { }: the catch variable is 'unknown' (TypeScript --strict), narrowed with
      // `e instanceof Error`. Variables assigned in the try block may hold any of the values they had
      // in it when the error happened, so their narrowings are dropped for the catch block.
      pushScope();
      checkStmt(s->body);
      popScope();
      invalidateAssigned(s->body);
      pushScope();
      if (!s->decls.empty()) {
        VarDecl& d = s->decls[0];
        if (d.type && !(d.type->k == TNK::Ref && d.type->name == "unknown" && d.type->args.empty()))
          err(d.type->loc, "a catch variable can only be annotated as 'unknown'");
        d.type = nullptr;
        bindPattern(d, T.caught(), false, false, d.loc);
      }
      checkStmt(s->els);
      popScope();
      invalidateAssigned(s->body);
      invalidateAssigned(s->els);
      break;
    }
    case SK::ForIn:
      err(s->loc, "for...in is not supported");
      break;
  }
}

}  // namespace e7
