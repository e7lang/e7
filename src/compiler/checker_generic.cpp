// Checker: generics, compiled by monomorphization.
//
// A generic declaration (function, arrow function assigned to a const, class, interface, type alias,
// method) is a template (GenericSym) that is never checked itself. Every distinct list of type
// arguments gets an instance: a copy of the declaration's syntax tree, checked in a scope where the
// type parameters are aliases of the concrete types. Instances are ordinary functions, classes and
// types, so the code generator and the VM need nothing new.
#include <cstdio>

#include "checker.h"
#include "intrinsics.h"

namespace e7 {

namespace {
Expr* strip(Expr* e) {
  while (e && e->k == EK::Paren) e = e->a;
  return e;
}
constexpr int kMaxInstDepth = 24;       // nesting of instantiations (polymorphic recursion)
constexpr int kMaxInstances = 4000;     // per program
constexpr size_t kMaxPerTemplate = 500; // per generic declaration
}  // namespace

std::vector<Param>* GenericSym::fnParams() const {
  switch (kind) {
    case Func: return &decl->params;
    case Arrow: return &arrow->params;
    case Method: return &member->params;
    default: return nullptr;
  }
}
TypeNode* GenericSym::fnRet() const {
  switch (kind) {
    case Func: return decl->retType;
    case Arrow: return arrow->retType;
    case Method: return member->retType;
    default: return nullptr;
  }
}

// ------------------------------------------------------------------ syntax tree copies
// Templates are never checked, so their nodes carry no checker annotations: a member-wise copy
// plus copies of the children is a fresh tree.
Expr* Checker::cloneExpr(Expr* e) {
  if (!e) return nullptr;
  Expr* c = P.arena.expr(e->k, e->loc);
  *c = *e;
  c->a = cloneExpr(e->a);
  c->b = cloneExpr(e->b);
  c->c = cloneExpr(e->c);
  c->exprBody = cloneExpr(e->exprBody);
  for (auto& x : c->list) x = cloneExpr(x);
  for (auto& p : c->props) p.value = cloneExpr(p.value);
  cloneParams(c->params);
  c->body = cloneStmt(e->body);
  return c;
}

void Checker::cloneParams(std::vector<Param>& ps) {
  for (auto& p : ps) {
    p.init = cloneExpr(p.init);
    if (p.pattern) p.pattern = P.arena.decl(cloneDecl(*p.pattern));
    p.sym = nullptr;
    p.innerType = nullptr;
  }
}

VarDecl Checker::cloneDecl(const VarDecl& d) {
  VarDecl c = d;
  c.init = cloneExpr(d.init);
  for (auto& el : c.elems) el = cloneDecl(el);
  c.sym = nullptr;
  c.restType = nullptr;
  c.generic = nullptr;
  return c;
}

ClassMember Checker::cloneMember(const ClassMember& m) {
  ClassMember c = m;
  c.init = cloneExpr(m.init);
  cloneParams(c.params);
  c.body = cloneStmt(m.body);
  c.fn = nullptr;
  return c;
}

Stmt* Checker::cloneStmt(Stmt* s) {
  if (!s) return nullptr;
  Stmt* c = P.arena.stmt(s->k, s->loc);
  *c = *s;
  for (auto& d : c->decls) d = cloneDecl(d);
  cloneParams(c->params);
  c->body = cloneStmt(s->body);
  c->then = cloneStmt(s->then);
  c->els = cloneStmt(s->els);
  c->init = cloneStmt(s->init);
  for (auto& x : c->stmts) x = cloneStmt(x);
  c->expr = cloneExpr(s->expr);
  c->update = cloneExpr(s->update);
  for (auto& cs : c->cases) {
    cs.test = cloneExpr(cs.test);
    for (auto& x : cs.body) x = cloneStmt(x);
  }
  for (auto& m : c->members) m = cloneMember(m);
  for (auto& em : c->enumMembers) em.init = cloneExpr(em.init);
  c->fn = nullptr;
  c->cls = nullptr;
  c->generic = nullptr;
  return c;
}

// ------------------------------------------------------------------ templates and instances
Sym* Checker::lookupFrom(Scope* s, const std::string& name) {
  for (Scope* x = s; x; x = x->parent) {
    auto it = x->names.find(name);
    if (it != x->names.end()) return it->second;
  }
  return name.find('.') != std::string::npos ? lookupQualified(s, name) : nullptr;
}

GenericSym* Checker::newGeneric(GenericSym::Kind k, const std::string& name, std::vector<TypeParam>* params, Loc l) {
  GenericSym* g = P.make<GenericSym>();
  g->kind = k;
  g->name = name;
  g->loc = l;
  g->params = params;
  g->file = curFile;
  g->defScope = scope;
  return g;
}

Scope* Checker::bindTypeParams(GenericSym* g, const std::vector<Type*>& args) {
  scopePool.emplace_back(new Scope());
  Scope* sc = scopePool.back().get();
  sc->parent = g->defScope;
  sc->fn = g->defScope ? g->defScope->fn : nullptr;
  for (size_t i = 0; i < g->params->size() && i < args.size(); i++) {
    if (!args[i]) continue;
    AliasSym* a = P.make<AliasSym>();
    a->name = (*g->params)[i].name;
    a->type = args[i];
    a->state = 2;
    sc->names[a->name] = a;
  }
  return sc;
}

static std::string argsKey(const std::vector<Type*>& args) {
  std::string k;
  char b[32];
  for (Type* t : args) {
    snprintf(b, sizeof b, "%p,", (void*)t);
    k += b;
  }
  return k;
}

std::string Checker::instanceName(GenericSym* g, const std::vector<Type*>& args) {
  std::string n = g->name + "<";
  for (size_t i = 0; i < args.size(); i++) n += (i ? ", " : "") + typeName(args[i]);
  if (n.size() > 300) n = n.substr(0, 300) + "...";  // nested instances: names could grow exponentially
  return n + ">";
}

int Checker::curInstDepth() {
  int d = instNest;
  for (FuncSym* f = curFn; f; f = f->parent) {
    d = std::max(d, f->instDepth);
    if (f->cls) d = std::max(d, f->cls->instDepth);
  }
  return d;
}

bool Checker::instLimits(GenericSym* g, Loc l, const std::string& name) {
  if (curInstDepth() >= kMaxInstDepth) {
    err(l, "instantiating '" + name + "' is nested too deeply (more than " + std::to_string(kMaxInstDepth) +
               " levels): a generic that instantiates itself with ever larger type arguments (polymorphic "
               "recursion) cannot be compiled by monomorphization");
    return false;
  }
  if (instCount >= kMaxInstances || g->order.size() >= kMaxPerTemplate) {
    err(l, "too many instantiations of '" + g->name + "' (at most " + std::to_string(kMaxPerTemplate) +
               " per generic declaration and " + std::to_string(kMaxInstances) + " in a program)");
    return false;
  }
  instCount++;
  return true;
}

// Fills type arguments that are missing with the defaults and checks the constraints. Returns -1
// when complete, the index of the first parameter without a type (no default), or -2 after a
// constraint error was reported.
int Checker::completeTypeArgs(GenericSym* g, std::vector<Type*>& bound, Loc l) {
  Scope* s0 = scope;
  std::string f0 = curFile;
  int missing = -1;
  for (size_t i = 0; i < g->params->size(); i++) {
    if (bound[i]) continue;
    TypeParam& tp = (*g->params)[i];
    // nothing to infer from: the default, else the constraint (as tsc does), unless it refers to type parameters
    TypeNode* fallback = tp.def ? tp.def : (tp.constraint && !mentionsParams(g, tp.constraint) ? tp.constraint : nullptr);
    if (!fallback) { missing = (int)i; break; }
    std::vector<Type*> prefix(bound.begin(), bound.begin() + (std::ptrdiff_t)i);
    scope = bindTypeParams(g, prefix);
    curFile = g->file;
    bound[i] = resolveType(fallback);
    scope = s0;
    curFile = f0;
  }
  if (missing >= 0) return missing;
  bool ok = true;
  for (size_t i = 0; i < g->params->size(); i++) {
    TypeParam& tp = (*g->params)[i];
    if (!tp.constraint) continue;
    scope = bindTypeParams(g, bound);
    curFile = g->file;
    Type* ct = resolveType(tp.constraint);
    scope = s0;
    curFile = f0;
    Type* a = bound[i];
    if (a == ct || ct->t == TT::Unknown || a->t == TT::Unknown || assignable(a, ct)) continue;
    // object-type constraints of built-in types (T extends { length: number } with a string) are
    // checked where the instance uses them
    if (ct->t == TT::Object && a->t != TT::Object && a->t != TT::Class) continue;
    err(l, "type '" + typeName(a) + "' does not satisfy the constraint '" + typeName(ct) + "' of type parameter '" +
               tp.name + "' of '" + g->name + "'");
    ok = false;
  }
  return ok ? -1 : -2;
}

bool Checker::explicitTypeArgs(GenericSym* g, const std::vector<TypeNode*>& nodes, Loc l, std::vector<Type*>& out) {
  size_t np = g->params->size();
  size_t required = 0;
  for (size_t i = 0; i < np; i++)
    if (!(*g->params)[i].def) required = i + 1;
  if (nodes.size() > np || nodes.size() < required) {
    err(l, "'" + g->name + "' expects " + (required == np ? std::to_string(np) : std::to_string(required) + "-" + std::to_string(np)) +
               " type argument(s), got " + std::to_string(nodes.size()));
    return false;
  }
  out.assign(np, nullptr);
  for (size_t i = 0; i < nodes.size(); i++) out[i] = resolveType(nodes[i]);
  for (Type* t : out)
    if (t && t->t == TT::Unknown) return false;
  return completeTypeArgs(g, out, l) == -1;
}

static bool mentionsName(TypeNode* n, const std::string& name) {
  if (!n) return false;
  if (n->k == TNK::Ref && n->args.empty() && n->name == name) return true;
  for (TypeNode* a : n->args)
    if (mentionsName(a, name)) return true;
  if (mentionsName(n->elem, name) || mentionsName(n->ret, name)) return true;
  for (auto& p : n->params)
    if (mentionsName(p.type, name)) return true;
  for (auto& m : n->members)
    if (mentionsName(m.type, name)) return true;
  return false;
}

// Does the type syntax mention a type parameter of g?
bool Checker::mentionsParams(GenericSym* g, TypeNode* n) {
  if (!n) return false;
  if (n->k == TNK::Ref && n->args.empty())
    for (auto& tp : *g->params)
      if (tp.name == n->name) return true;
  for (TypeNode* a : n->args)
    if (mentionsParams(g, a)) return true;
  if (mentionsParams(g, n->elem) || mentionsParams(g, n->ret)) return true;
  for (auto& p : n->params)
    if (mentionsParams(g, p.type)) return true;
  for (auto& m : n->members)
    if (mentionsParams(g, m.type)) return true;
  return false;
}

// The type of a template's type syntax with the bound parameters substituted (unbound: unknown,
// which contextual typing treats as "no context").
Type* Checker::substPartial(GenericSym* g, const std::vector<Type*>& bound, TypeNode* n) {
  std::vector<Type*> args = bound;
  for (auto& a : args)
    if (!a) a = T.unknown();
  Scope* s0 = scope;
  std::string f0 = curFile;
  scope = bindTypeParams(g, args);
  curFile = g->file;
  Type* t = resolveType(n);
  scope = s0;
  curFile = f0;
  return t;
}

// One inference candidate t for a type parameter (slot): the first one, widened to a nullable type
// or a common base when later ones need it; a conflict is reported later (argument types).
static void bindCandidate(Checker* ck, Types& T, Type*& slot, Type* t,
                          const std::function<bool(Type*, Type*)>& assignable) {
  (void)ck;
  if (!t || t->t == TT::Never || t->t == TT::Unknown) return;
  if (!slot) { slot = t; return; }
  if (slot == t || assignable(t, slot)) return;
  if (assignable(slot, t)) { slot = t; return; }
  bool sn = slot->t == TT::Null || slot->t == TT::Undefined;
  bool tn = t->t == TT::Null || t->t == TT::Undefined;
  if (sn || tn || slot->t == TT::Optional || t->t == TT::Optional) {
    Type* sb = sn ? nullptr : baseOf(slot);
    Type* tb = tn ? nullptr : baseOf(t);
    if (sb && tb && sb != tb) return;
    Type* b = sb ? sb : tb;
    if (!b) return;
    bool hasNull = slot->t == TT::Null || t->t == TT::Null || (slot->t == TT::Optional && slot->hasNull) ||
                   (t->t == TT::Optional && t->hasNull);
    bool hasUndef = slot->t == TT::Undefined || t->t == TT::Undefined || (slot->t == TT::Optional && slot->hasUndef) ||
                    (t->t == TT::Optional && t->hasUndef);
    slot = T.optional(b, hasNull, hasUndef);
  }
}

// Infers type parameters of g by matching type syntax n (from the template) against type t.
void Checker::inferMatch(GenericSym* g, std::vector<Type*>& bound, TypeNode* n, Type* t, int depth) {
  if (!n || !t || t->t == TT::Unknown || depth > 64) return;
  auto rec = [&](TypeNode* a, Type* b) { inferMatch(g, bound, a, b, depth + 1); };
  auto asg = [&](Type* a, Type* b) { return assignable(a, b); };
  switch (n->k) {
    case TNK::Ref: {
      if (n->args.empty()) {
        for (size_t i = 0; i < g->params->size(); i++)
          if ((*g->params)[i].name == n->name) {
            bindCandidate(this, T, bound[i], t, asg);
            return;
          }
        return;
      }
      const std::string& nm = n->name;
      if ((nm == "Array" || nm == "ReadonlyArray") && n->args.size() == 1) {
        if (t->t == TT::Array) rec(n->args[0], t->elem);
        return;
      }
      if ((nm == "Map" || nm == "ReadonlyMap") && n->args.size() == 2) {
        if (t->t == TT::Map) { rec(n->args[0], t->key); rec(n->args[1], t->val); }
        return;
      }
      if ((nm == "Set" || nm == "ReadonlySet") && n->args.size() == 1) {
        if (t->t == TT::Set) rec(n->args[0], t->elem);
        return;
      }
      if ((nm == "Generator" || nm == "Iterator" || nm == "IterableIterator" || nm == "IteratorObject") && t->t == TT::Generator) {
        rec(n->args[0], t->elem);
        if (n->args.size() > 1 && t->ret && t->ret->t != TT::Void) rec(n->args[1], t->ret);
        if (n->args.size() > 2 && t->val && t->val->t != TT::Void) rec(n->args[2], t->val);
        return;
      }
      if ((nm == "AsyncGenerator" || nm == "AsyncIterator" || nm == "AsyncIterableIterator" || nm == "AsyncIterable") &&
          t->t == TT::AsyncGenerator && !n->args.empty()) {
        rec(n->args[0], t->elem);
        if (n->args.size() > 1 && t->ret && t->ret->t != TT::Void) rec(n->args[1], t->ret);
        if (n->args.size() > 2 && t->val && t->val->t != TT::Void) rec(n->args[2], t->val);
        return;
      }
      if (nm == "Promise" && n->args.size() == 1) {
        if (t->t == TT::Promise) rec(n->args[0], t->elem);
        return;
      }
      Sym* s = lookupFrom(g->defScope, nm);
      if (!s || s->k != SymK::Generic) return;
      GenericSym* h = static_cast<GenericSym*>(s);
      std::vector<Type*>* targs = nullptr;
      if (t->t == TT::Class) {
        for (ClassSym* c = t->cls; c && !targs; c = c->parent)
          if (c->origin == h) targs = &c->typeArgs;
      } else if (t->t == TT::Object && t->shape->origin == h) {
        targs = &t->shape->typeArgs;
      }
      if (targs) {
        for (size_t j = 0; j < n->args.size() && j < targs->size(); j++) rec(n->args[j], (*targs)[j]);
      } else if (h->kind == GenericSym::Alias || h->kind == GenericSym::Interface) {
        // a generic alias or interface given another type (e.g. an object literal's): match its
        // definition, then the arguments against what its parameters got
        std::vector<Type*> sub(h->params->size(), nullptr);
        inferMatch(h, sub, h->decl->typeBody, t, depth + 1);
        for (size_t j = 0; j < n->args.size() && j < sub.size(); j++)
          if (sub[j]) rec(n->args[j], sub[j]);
      }
      return;
    }
    case TNK::Array:
      if (t->t == TT::Array) rec(n->elem, t->elem);
      return;
    case TNK::Tuple:
      if (t->t == TT::Tuple && t->elems.size() == n->args.size())
        for (size_t i = 0; i < n->args.size(); i++) rec(n->args[i], t->elems[i]);
      return;
    case TNK::Union: {
      TypeNode* m = nullptr;
      int cnt = 0;
      for (TypeNode* a : n->args) {
        bool nullish = a->k == TNK::Ref && a->args.empty() && (a->name == "null" || a->name == "undefined");
        if (!nullish && a->k != TNK::Literal) { m = a; cnt++; }
      }
      if (cnt != 1 || t->t == TT::Null || t->t == TT::Undefined) return;
      rec(m, baseOf(t));
      return;
    }
    case TNK::Func:
      if (t->t != TT::Func) return;
      for (size_t i = 0; i < n->params.size() && i < t->params.size(); i++) rec(n->params[i].type, t->params[i]);
      rec(n->ret, t->ret);
      return;
    case TNK::Object:
      for (auto& m : n->members) {
        if (m.method) {
          std::string key = m.accessor == 1 ? "get " + m.name : m.accessor == 2 ? "set " + m.name : m.name;
          if (t->t == TT::Object) {
            if (const ObjShape::Method* om = t->shape->findMethod(key)) rec(m.type, om->fn);
          } else if (t->t == TT::Class) {
            auto it = t->cls->methodIdx.find(key);
            if (it != t->cls->methodIdx.end() && t->cls->vtable[it->second].fn->type)
              rec(m.type, t->cls->vtable[it->second].fn->type);
          }
          continue;
        }
        Type* pt = nullptr;
        if (t->t == TT::Object) {
          if (const ObjShape::Prop* p = t->shape->findProp(m.name)) pt = p->type;
        } else if (t->t == TT::Class) {
          auto it = t->cls->fieldIdx.find(m.name);
          if (it != t->cls->fieldIdx.end()) pt = t->cls->fields[it->second].type;
        }
        if (!pt) continue;
        if (m.optional && pt->t == TT::Optional && pt->hasUndef) pt = pt->hasNull ? T.optional(pt->elem, true, false) : pt->elem;
        rec(m.type, pt);
      }
      return;
    case TNK::Literal:
      return;
  }
}

// Type arguments of a call to a generic function / method / constructor: explicit ones, or inferred
// from the arguments (first the ones that are not functions, then function literals contextually
// typed with what is known), then from the expected type of the result, then the defaults.
bool Checker::inferCall(GenericSym* g, std::vector<Param>* params, TypeNode* ret, std::vector<Expr*>& args,
                        const std::vector<TypeNode*>& explicitArgs, Type* expected, Loc l, std::vector<Type*>& out) {
  size_t np = g->params->size();
  out.assign(np, nullptr);
  if (!explicitArgs.empty()) return explicitTypeArgs(g, explicitArgs, l, out);
  expandSpreads(args);
  if (params) {
    auto paramNode = [&](size_t i, bool spread) -> TypeNode* {
      if (i < params->size() && !(*params)[i].rest) return spread ? nullptr : (*params)[i].type;
      if (params->empty() || !params->back().rest || i + 1 < params->size()) return nullptr;
      TypeNode* rn = params->back().type;
      if (!rn || spread) return rn;
      if (rn->k == TNK::Array) return rn->elem;
      if (rn->k == TNK::Ref && (rn->name == "Array" || rn->name == "ReadonlyArray") && rn->args.size() == 1) return rn->args[0];
      return nullptr;
    };
    auto isFnArg = [&](Expr* x) {  // checked in the second pass, with the type parameters known so far
      x = strip(x);
      if (x->k == EK::Arrow) return true;
      if (x->k == EK::Array && x->list.empty()) return true;  // `[]` takes its element type from the others
      if (x->k != EK::Ident) return false;
      Sym* s = lookup(x->raw);
      return s && s->k == SymK::Generic;
    };
    for (int pass = 0; pass < 2; pass++)
      for (size_t i = 0; i < args.size(); i++) {
        Expr* a = args[i];
        bool spread = a->k == EK::Spread;
        bool fnArg = !spread && isFnArg(a);
        if ((pass == 1) != fnArg) continue;
        TypeNode* pn = paramNode(i, spread);
        if (!pn || !mentionsParams(g, pn)) continue;
        Type* at = a->type;
        if (!at) {
          // contextual type: the parameter type with what is known so far (function literals),
          // or the full parameter type once every type parameter it uses is known
          bool allBound = true;
          for (size_t k = 0; k < np && allBound; k++)
            if (!out[k] && mentionsName(pn, (*g->params)[k].name)) allBound = false;
          Type* exp = (fnArg || allBound) ? substPartial(g, out, pn) : nullptr;
          at = check(a, exp);
        }
        inferMatch(g, out, pn, at);
      }
  }
  if (expected && ret) {
    std::vector<Type*> fromRet(np, nullptr);
    inferMatch(g, fromRet, ret, expected);
    for (size_t i = 0; i < np; i++)
      if (!out[i]) out[i] = fromRet[i];
  }
  int r = completeTypeArgs(g, out, l);
  if (r >= 0)
    err(l, "cannot infer the type argument '" + (*g->params)[r].name + "' of '" + g->name + "'; write it: " +
               g->name + "<...>(...)");
  return r == -1;
}

// ------------------------------------------------------------------ instantiation
FuncSym* Checker::instantiateFunc(GenericSym* g, const std::vector<Type*>& args, Loc l) {
  std::string key = argsKey(args);
  auto it = g->instances.find(key);
  if (it != g->instances.end()) {
    if (!it->second) err(l, "a generic arrow function cannot use itself; declare it with 'function'");
    return static_cast<FuncSym*>(it->second);
  }
  std::string nm = instanceName(g, args);
  if (!instLimits(g, l, nm)) return nullptr;
  int depth = curInstDepth() + 1;
  Scope* s0 = scope;
  FuncSym* f0 = curFn;
  std::string file0 = curFile;
  auto loops0 = loops;
  auto labels0 = pendingLabels;
  ClassSym* static0 = staticInitClass;
  int nest0 = instNest;
  loops.clear();
  pendingLabels.clear();
  staticInitClass = nullptr;
  instNest = depth;
  curFile = g->file;
  Scope* sc = bindTypeParams(g, args);
  instCtx.push_back(nm);
  FuncSym* f = nullptr;
  if (g->kind == GenericSym::Arrow) {
    g->instances[key] = nullptr;  // being checked (a use of itself is an error)
    Expr* a = cloneExpr(g->arrow);
    a->typeParams.clear();
    a->raw = g->name;
    scope = sc;
    curFn = g->parentFn;
    check(a, nullptr);
    f = a->fn;
    if (f) {
      f->name = nm;
      f->qualName = (g->parentFn && !g->parentFn->isInit ? g->parentFn->qualName + "." : "") + nm;
      f->instName = nm;
      f->instDepth = depth;
      if (!g->topLevel) {
        f->selfVar = newVar(nm, f->type, true, l);
        f->selfVar->owner = g->parentFn;
      }
    }
    g->instances[key] = f;
  } else if (g->kind == GenericSym::Func) {
    Stmt* s = cloneStmt(g->decl);
    s->typeParams.clear();
    curFn = g->topLevel ? nullptr : g->parentFn;
    f = newFunc(nm, &s->params, s->retType, s->body, nullptr);
    f->isGenerator = s->isGenerator;
    f->isAsync = s->isAsync;
    f->declStmt = s;
    s->fn = f;
    f->defScope = sc;
    f->instName = nm;
    f->instDepth = depth;
    if (g->topLevel) {
      f->isTopLevel = true;
      f->parent = nullptr;
      f->qualName = nm;
    } else {
      f->parent = g->parentFn;
      f->qualName = (g->parentFn && !g->parentFn->isInit ? g->parentFn->qualName + "." : "") + nm;
      f->selfVar = newVar(nm, nullptr, true, l);
      f->selfVar->owner = g->parentFn;
    }
    g->instances[key] = f;
    scope = sc;
    resolveSig(f);
    if (f->selfVar) f->selfVar->type = f->type;
  } else if (g->kind == GenericSym::Method) {
    ClassSym* c = g->ownerClass;
    memberPool.emplace_back(new ClassMember(cloneMember(*g->member)));
    ClassMember* m = memberPool.back().get();
    m->typeParams.clear();
    curFn = nullptr;
    f = newFunc(nm, &m->params, m->retType, m->body, nullptr);
    f->isGenerator = m->isGenerator;
    f->isAsync = m->isAsync;
    m->fn = f;
    f->loc = m->loc;
    f->parent = nullptr;
    f->cls = c;
    f->isTopLevel = true;
    f->isStatic = m->isStatic;
    f->qualName = c->name + "." + nm;
    f->defScope = sc;
    f->instName = f->qualName;
    f->instDepth = std::max(depth, c->instDepth);
    if (!m->isStatic) {
      f->isMethod = true;
      f->thisVar = newVar("this", c->type, true, m->loc);
      f->thisVar->isThis = true;
      f->thisVar->owner = f;
    }
    g->instances[key] = f;
    scope = sc;
    resolveSig(f);
  }
  if (f) g->order.push_back(f);
  instCtx.pop_back();
  scope = s0;
  curFn = f0;
  curFile = file0;
  loops = loops0;
  pendingLabels = labels0;
  staticInitClass = static0;
  instNest = nest0;
  return f;
}

ClassSym* Checker::instantiateClass(GenericSym* g, const std::vector<Type*>& args, Loc l) {
  std::string key = argsKey(args);
  auto it = g->instances.find(key);
  if (it != g->instances.end()) return static_cast<ClassSym*>(it->second);
  std::string nm = instanceName(g, args);
  if (!instLimits(g, l, nm)) return nullptr;
  ClassSym* c = P.make<ClassSym>();
  c->name = nm;
  c->rtName = g->name;
  c->decl = cloneStmt(g->decl);
  c->decl->typeParams.clear();
  c->decl->cls = c;
  c->file = g->file;
  c->loc = g->loc;
  c->type = T.classType(c);
  c->origin = g;
  c->typeArgs = args;
  c->genericRole = 2;
  c->defScope = bindTypeParams(g, args);
  c->instDepth = curInstDepth() + 1;
  g->instances[key] = c;
  g->order.push_back(c);
  int nest0 = instNest;
  instNest = c->instDepth;
  instCtx.push_back(nm);
  resolveClass(c);
  instCtx.pop_back();
  instNest = nest0;
  return c;
}

Type* Checker::instantiateType(GenericSym* g, const std::vector<Type*>& args, Loc l) {
  if (g->kind == GenericSym::Class) {
    ClassSym* c = instantiateClass(g, args, l);
    return c ? c->type : T.unknown();
  }
  std::string key = argsKey(args);
  std::string nm = instanceName(g, args);
  Scope* s0 = scope;
  std::string f0 = curFile;
  if (g->kind == GenericSym::Interface) {
    auto it = g->instances.find(key);
    InterfaceSym* is;
    if (it != g->instances.end()) {
      is = static_cast<InterfaceSym*>(it->second);
    } else {
      if (!instLimits(g, l, nm)) return T.unknown();
      is = P.make<InterfaceSym>();
      is->name = nm;
      is->loc = g->loc;
      is->decl = g->decl;
      is->defScope = bindTypeParams(g, args);
      is->origin = g;
      is->typeArgs = args;
      g->instances[key] = is;
      g->order.push_back(is);
    }
    curFile = g->file;
    int nest0 = instNest;
    instNest = curInstDepth() + 1;
    instCtx.push_back(nm);
    resolveInterface(is);
    instCtx.pop_back();
    instNest = nest0;
    curFile = f0;
    return is->type ? is->type : T.unknown();
  }
  if (g->kind == GenericSym::Alias) {
    auto it = g->typeInstances.find(key);
    if (it != g->typeInstances.end()) return it->second;
    if (g->resolving.count(key)) return errT(l, "type alias '" + nm + "' circularly references itself");
    if (!instLimits(g, l, nm)) return T.unknown();
    g->resolving[key] = true;
    scope = bindTypeParams(g, args);
    curFile = g->file;
    int nest0 = instNest;
    instNest = curInstDepth() + 1;
    instCtx.push_back(nm);
    Type* t = resolveType(g->decl->typeBody);
    instCtx.pop_back();
    instNest = nest0;
    scope = s0;
    curFile = f0;
    g->resolving.erase(key);
    g->typeInstances[key] = t;
    return t;
  }
  return errT(l, "'" + g->name + "' is a generic function, not a type");
}

// Box<number> in a type position
Type* Checker::genericNamedType(GenericSym* g, const std::vector<TypeNode*>& argNodes, Loc l) {
  if (g->kind != GenericSym::Class && g->kind != GenericSym::Interface && g->kind != GenericSym::Alias)
    return errT(l, "'" + g->name + "' is a generic function, not a type");
  size_t np = g->params->size();
  if (argNodes.size() > np) return errT(l, "generic type '" + g->name + "' takes " + std::to_string(np) + " type argument(s)");
  std::vector<Type*> args(np, nullptr);
  for (size_t i = 0; i < argNodes.size(); i++) {
    args[i] = resolveType(argNodes[i]);
    if (args[i]->t == TT::Unknown) return T.unknown();
  }
  int r = completeTypeArgs(g, args, l);
  if (r == -2) return T.unknown();
  if (r >= 0) {
    size_t required = 0;
    for (size_t i = 0; i < np; i++)
      if (!(*g->params)[i].def) required = i + 1;
    return errT(l, "generic type '" + g->name + "' requires " + std::to_string(required) + " type argument(s)");
  }
  return instantiateType(g, args, l);
}

// A generic function used as a value: its type arguments come from the contextual function type.
FuncSym* Checker::genericFuncValue(GenericSym* g, Expr* e, Type* expected) {
  if (g->kind != GenericSym::Func && g->kind != GenericSym::Arrow) {
    err(e->loc, "'" + g->name + "' cannot be used as a value");
    return nullptr;
  }
  Type* exp = expected ? baseOf(expected) : nullptr;
  std::vector<Type*> bound(g->params->size(), nullptr);
  if (exp && exp->t == TT::Func) {
    std::vector<Param>* ps = g->fnParams();
    for (size_t i = 0; i < ps->size() && i < exp->params.size(); i++)
      if ((*ps)[i].type && !(*ps)[i].rest) inferMatch(g, bound, (*ps)[i].type, exp->params[i]);
    if (g->fnRet() && exp->ret->t != TT::Unknown && exp->ret->t != TT::Void) inferMatch(g, bound, g->fnRet(), exp->ret);
  }
  int r = completeTypeArgs(g, bound, e->loc);
  if (r == -2) return nullptr;
  if (r >= 0) {
    err(e->loc, "generic function '" + g->name + "' needs a contextual function type to be used as a value (type argument '" +
                    (*g->params)[r].name + "' unknown); wrap it in an arrow function, e.g. (x: number) => " + g->name + "(x)");
    return nullptr;
  }
  return instantiateFunc(g, bound, e->loc);
}

ClassSym* Checker::classOf(Sym* s) {
  if (!s) return nullptr;
  if (s->k == SymK::Class) return static_cast<ClassSym*>(s);
  if (s->k == SymK::Generic && static_cast<GenericSym*>(s)->kind == GenericSym::Class) {
    ClassSym* h = static_cast<GenericSym*>(s)->holder;
    resolveClass(h);
    return h;
  }
  return nullptr;
}

}  // namespace e7
