// Checker: relations, functions and statements.
#include <algorithm>
#include <functional>

#include "checker.h"
#include "intrinsics.h"

namespace e7 {

// ------------------------------------------------------------------ relations
bool Checker::structurallyAssignable(Type* src, Type* dst) {
  if (dst->t != TT::Object) return false;
  // recursive types: assume the pair being compared is compatible while comparing its members
  auto key = std::make_pair(src, dst);
  if (assumedAssignable_.count(key)) return true;
  assumedAssignable_.insert(key);
  bool ok = structurallyAssignableImpl(src, dst);
  assumedAssignable_.erase(key);
  return ok;
}

bool Checker::structurallyAssignableImpl(Type* src, Type* dst) {
  ObjShape* ds = dst->shape;
  // the type of an object literal with accessors holds only such object literals (whose accessors are
  // own properties): values of other types may be class instances
  if (ds->ownAccessors && !(src->t == TT::Object && src->shape->ownAccessors)) return false;
  if (src->t == TT::Class) {
    ClassSym* c = src->cls;
    resolveClass(c);
    for (auto& p : ds->props) {
      auto it = c->fieldIdx.find(p.name);
      Type* ft;
      if (it != c->fieldIdx.end()) ft = c->fields[it->second].type;
      else if (Type* at = c->abstractProp(p.name)) ft = at;  // read by name, like interface properties
      else return false;
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

// whether every value of literal type a is one of literal type b's (same base)
static bool litSubset(const Type* a, const Type* b) {
  if (a->t != b->t) return false;
  for (auto& s : a->strLits)
    if (!std::binary_search(b->strLits.begin(), b->strLits.end(), s)) return false;
  for (double d : a->numLits) {
    bool f = false;
    for (double e : b->numLits) f = f || e == d || (e != e && d != d);
    if (!f) return false;
  }
  return (a->boolLits & ~b->boolLits) == 0;
}

// Why an object type with accessors is not assignable to another object type (for error messages)
std::string Checker::accessorHint(Type* src, Type* dst) {
  Type* s = baseOf(src);
  Type* d = baseOf(dst);
  if (s->t == TT::Array && d->t == TT::Array) return accessorHint(s->elem, d->elem);
  if (d->t == TT::Object && d->shape->ownAccessors && !(s->t == TT::Object && s->shape->ownAccessors))
    return ": the type of an object literal with accessors only holds such object literals";
  if (s->t == TT::Class && d->t == TT::Object && !d->shape->name.empty()) {
    // a class without an optional property of an interface: `implements` gives it one (absent)
    for (auto& p : d->shape->props)
      if (!s->cls->fieldIdx.count(p.name) && !s->cls->abstractProp(p.name) && s->cls->getterSlot(p.name) < 0) {
        if (!p.optional) return "";
        return ": class '" + s->cls->name + "' does not declare the optional property '" + p.name + "'; write 'class " + s->cls->name +
               " implements " + d->shape->name + "' (the class then has it, as undefined until it is set)";
      }
    return "";
  }
  if (s->t != TT::Object || d->t != TT::Object) return "";
  ObjShape* ss = s->shape;
  ObjShape* ds = d->shape;
  for (auto& p : ds->props)
    if (!ss->findProp(p.name) && (ss->findMethod("get " + p.name) || ss->findMethod("set " + p.name)))
      return ": property '" + p.name + "' is an accessor in '" + typeName(s) + "'; e7 reads the properties of '" + typeName(d) +
             "' as fields, so declare it there as 'get " + p.name + "(): " + typeName(p.type) + "'";
  for (auto& m : ds->methods)
    if ((m.name.compare(0, 4, "get ") == 0 || m.name.compare(0, 4, "set ") == 0) && !ss->findMethod(m.name))
      return std::string(": '") + typeName(s) + "' has no " + (m.name[0] == 'g' ? "getter" : "setter") + " for '" + m.name.substr(4) + "'";
  return "";
}

bool Checker::assignable(Type* src, Type* dst) {
  if (src == dst) return true;
  if (src->t == TT::Unknown || dst->t == TT::Unknown || src->t == TT::Never) return true;
  // literal types (D5): a literal type is its base type with fewer values
  if (dst->lit) return src->lit && litSubset(src, dst);
  // a number to a numeric enum, as in TypeScript (a byte read off the wire: bytes[0] as MsgType, or
  // const t: MsgType = n); a number literal must be one of the enum's values
  if (dst->t == TT::Enum && dst->en && !dst->en->isString && src->t == TT::Number) {
    if (!src->lit) return true;
    for (double v : src->numLits) {
      bool found = false;
      for (auto& m : dst->en->members) found = found || m.num == v;
      if (!found) return false;
    }
    return true;
  }
  // "a" to "a" | "b" | undefined: the literal against the optional's literal type
  if (src->lit) return (dst->t == TT::Optional && dst->elem->lit && litSubset(src, dst->elem)) || assignable(src->litBase, dst);
  switch (dst->t) {
    case TT::Optional: {
      if (src->t == TT::Null) return dst->hasNull;
      if (src->t == TT::Undefined) return dst->hasUndef;
      if (src->t == TT::Optional) {
        if (src->hasNull && !dst->hasNull) return false;
        if (src->hasUndef && !dst->hasUndef) return false;
        if (src->elem->lit && dst->elem->lit && kindOf(src->elem) == kindOf(dst->elem)) return litSubset(src->elem, dst->elem);
        if (kindOf(src->elem) != K_R || kindOf(dst->elem) != K_R) return src->elem == dst->elem;
        return assignable(src->elem, dst->elem);
      }
      if (kindOf(src) != K_R || kindOf(dst->elem) != K_R) return src == dst->elem;
      return assignable(src, dst->elem);
    }
    case TT::Class:
      return src->t == TT::Class && isAncestor(dst->cls, src->cls);
    case TT::Array:
      // a mutable array is assignable to a readonly one, not the other way round (as in TypeScript)
      if (src->t == TT::Array && src->ro && !dst->ro) return false;
      if (src->t == TT::Array && src->elem == dst->elem) return true;
      // arrays of references are covariant (as in TypeScript); representation is identical
      return src->t == TT::Array && kindOf(src->elem) == K_R && kindOf(dst->elem) == K_R &&
             !isBoxedOptional(src->elem) && !isBoxedOptional(dst->elem) && src->elem->t != TT::Func &&
             dst->elem->t != TT::Func && assignable(src->elem, dst->elem);
    case TT::Tuple:
      return src->t == TT::Tuple && dst->ro && src->elems == dst->elems;
    case TT::Map:
      return src->t == TT::Map && dst->ro && src->key == dst->key && src->val == dst->val;
    case TT::Set:
      return src->t == TT::Set && dst->ro && src->elem == dst->elem;
    case TT::Object:
      return structurallyAssignable(src, dst);
    case TT::Generator:
      // all iterator types are coroutines at run time: Generator -> Iterator / IterableIterator, with the
      // same value types (a target without TReturn / TNext accepts any)
      return src->t == TT::Generator && (dst->gk != 0 || src->gk == 0) && src->elem == dst->elem &&
             (dst->ret->t == TT::Void || src->ret == dst->ret) && (dst->val->t == TT::Void || src->val == dst->val);
    case TT::AsyncGenerator:
      // the same for async generators: AsyncGenerator -> AsyncIterator / AsyncIterableIterator / AsyncIterable
      return src->t == TT::AsyncGenerator && (dst->gk != 0 || src->gk == 0) && src->elem == dst->elem &&
             (dst->ret->t == TT::Void || src->ret == dst->ret) && (dst->val->t == TT::Void || src->val == dst->val);
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

static Expr* stripParens(Expr* e) {
  while (e && e->k == EK::Paren) e = e->a;
  return e;
}

// The literal type of a constant expression ("a", 1, -2, true, `x`, c ? "a" : "b"), or null
Type* Checker::constLit(Expr* e) {
  while (e && (e->k == EK::Paren || (e->k == EK::Conv && e->conv == ConvKind::None))) e = e->a;
  if (!e) return nullptr;
  switch (e->k) {
    case EK::Str: return T.strLit({e->str});
    case EK::Template: return e->list.empty() && e->quasis.size() == 1 ? T.strLit({e->quasis[0]}) : nullptr;
    case EK::Num: return e->type && e->type->t == TT::Number ? T.numLit({e->num}) : nullptr;
    case EK::Bool: return T.boolLit(e->bval ? 2 : 1);
    case EK::Unary:
      if (e->raw == "-" && e->a && stripParens(e->a)->k == EK::Num && e->type && e->type->t == TT::Number)
        return T.numLit({-stripParens(e->a)->num});
      return nullptr;
    case EK::Cond: {
      Type* a = constLit(e->b);
      Type* b = a ? constLit(e->c) : nullptr;
      return b ? T.litUnion(a, b) : nullptr;
    }
    default:
      return e->type && e->type->lit ? e->type : nullptr;
  }
}

// a variable's type from its initializer: a const keeps the literal type of a constant ("a"), other
// variables get the base type (TypeScript's widening)
bool freshLiteral(Expr* x);  // (checker_expr.cpp)

Type* Checker::inferVarType(Expr* init, Type* t, bool isConst) {
  // (let r = d of a d: Dir keeps Dir: tsc widens only fresh literals, let k = "a")
  if (!isConst) return t->lit && init && !freshLiteral(init) ? t : widen(t);
  if (Type* c = init && (t->t == TT::String || t->t == TT::Number || t->t == TT::Bool) ? constLit(init) : nullptr) return c;
  return t;
}

bool Checker::coerce(Expr*& e, Type* target, const char* ctx) {
  Type* t = e->type;
  if (!t || !target) return true;
  if (t == target) return true;
  // const unreachable: never = x in the default of an exhaustive switch (x narrowed to never)
  if (target->t == TT::Never) {
    Expr* x = e;
    while (x && (x->k == EK::Paren || (x->k == EK::Conv && x->conv == ConvKind::None))) x = x->a;
    if (x && x->neverNarrowed) {
      // the value is kept as it is (boxed when it is a number, boolean or integer)
      Kind k = kindOf(x->type);
      Expr* c = P.arena.expr(EK::Conv, e->loc);
      c->conv = k == K_F ? ConvKind::NumToBox : k == K_I ? ConvKind::IntToBox : ConvKind::None;
      c->a = e;
      c->type = target;
      e = c;
      return true;
    }
  }
  // a constant of a literal type's base: its value decides ("GET" is a "GET" | "POST")
  if (baseOf(target)->lit && !t->lit && (t->t == TT::String || t->t == TT::Number || t->t == TT::Bool))
    if (Type* c = constLit(e)) t = c;
  // a number constant for a numeric enum must be one of its values
  if (baseOf(target)->t == TT::Enum && t->t == TT::Number && !t->lit)
    if (Type* c = constLit(e)) t = c;
  if (target->t == TT::Void && ctx && std::string(ctx) == "return") return false;
  if (!assignable(t, target)) {
    err(e->loc, "type '" + typeName(t) + "' is not assignable to type '" + typeName(target) + "'" + accessorHint(t, target));
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

// the type a mutable location gets from a value: literal types become their base type (TypeScript
// widens `let x = "a"` to string)
Type* Checker::widen(Type* t) {
  if (!t) return t;
  if (t->lit) return t->litBase;
  if (t->t == TT::Optional && t->elem->lit) return T.optional(t->elem->litBase, t->hasNull, t->hasUndef);
  return t;
}

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
  // literal types of one base ("red" and "green" of an `as const` tuple): their union
  if (a->lit && b->lit && a->litBase == b->litBase)
    if (Type* u = T.litUnion(a, b)) return u;
  return errT(l, std::string("incompatible types in ") + what + ": '" + typeName(a) + "' and '" + typeName(b) + "'");
}

// ------------------------------------------------------------------ functions
// the type that `return` statements in f's body return: TReturn of a generator, T of an async
// function's Promise<T>, else the function's return type
Type* Checker::bodyRet(FuncSym* f) {
  Type* r = f->type->ret;
  if (f->isGenerator) return (r->t == TT::Generator || r->t == TT::AsyncGenerator) && r->ret ? r->ret : T.voidT();
  if (f->isAsync) return r->t == TT::Promise ? r->elem : T.voidT();
  return r;
}

// the declared return type of a generator / async function must be Generator / Promise
void Checker::checkCoroutineRet(FuncSym* f, Type* ret, Loc l) {
  if (f->isGenerator && !f->isAsync && ret->t != TT::Generator && ret->t != TT::Unknown)
    err(l, "the return type of a generator must be Generator<T>, Iterator<T> or IterableIterator<T>, not '" + typeName(ret) + "'");
  if (f->isGenerator && f->isAsync && ret->t != TT::AsyncGenerator && ret->t != TT::Unknown)
    err(l, "the return type of an async generator must be AsyncGenerator<T>, AsyncIterator<T>, AsyncIterableIterator<T> or "
           "AsyncIterable<T>, not '" + typeName(ret) + "'");
  if (f->isAsync && !f->isGenerator && ret->t != TT::Promise && ret->t != TT::Unknown)
    err(l, "the return type of an async function must be Promise<T>, not '" + typeName(ret) + "'");
}

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
  auto savedStatic = staticInitClass;
  staticInitClass = nullptr;
  if (!f->instName.empty()) instCtx.push_back(f->instName);
  pushScope(true);
  checkBody(f);
  if (!f->instName.empty()) instCtx.pop_back();
  staticInitClass = savedStatic;
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
      // closure parameter with a default: `T | undefined` outside, T inside (see checkArrow)
      Type* pt = f->arrow && p.init && p.innerType ? p.innerType : f->type->params[i];
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
        Type* dt = check(p.init, base);
        coerce(p.init, base, "default value");
        // p: T | undefined = d: undefined is replaced by d (the prologue), so the body sees T
        if (!f->arrow && pt->t == TT::Optional && pt->hasUndef && dt && !hasUndefined(dt) && dt->t != TT::Unknown)
          setNarrow(varKey(v), T.optional(pt->elem, pt->hasNull, false));
      }
    }
  }
  Type* declaredRet = f->inferRet ? nullptr : bodyRet(f);
  f->returnTypes.clear();
  f->yieldTypes.clear();
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
    if (declaredRet && f->isAsync && !f->isGenerator && t->t == TT::Promise) {
      // async (): Promise<T> => p: its promise adopts p (as `return p` does)
      if (t->elem != declaredRet && !(declaredRet->t == TT::Void && t->elem->t == TT::Void))
        err(f->exprBody->loc, "type '" + typeName(t) + "' is not assignable to the return type 'Promise<" + typeName(declaredRet) + ">'");
    } else if (declaredRet) {
      if (declaredRet->t != TT::Void) coerce(f->exprBody, declaredRet, "return");
    } else {
      f->returnTypes.push_back(t);
    }
  } else if (f->body) {
    checkBlockStmts(f->body->stmts);
  }
  if (f->inferRet) {
    Type* r = nullptr;
    // `return;` and `return v`: v's type or undefined (as tsc infers)
    bool bare = false, valued = false;
    for (Type* t : f->returnTypes) (t->t == TT::Void ? bare : valued) = true;
    for (Type* t : f->returnTypes) {
      if (bare && valued && t->t == TT::Void) continue;
      r = r ? unify(r, t, f->loc, "return statements") : t;
    }
    // (a fresh literal returned, return 5: its base type, as tsc widens an inferred return type; not "b" as
    // const or a value of a declared literal type)
    if (r && r->lit) {
      bool fresh = true;
      std::function<bool(Expr*)> freshLit = [&](Expr* x) -> bool {
        while (x && (x->k == EK::Paren || (x->k == EK::Conv && x->conv == ConvKind::None))) x = x->a;
        if (!x) return false;
        switch (x->k) {
          case EK::Str: case EK::Num: case EK::Bool: case EK::BigLit: return true;
          case EK::Template: return x->list.empty();
          case EK::Unary: return x->raw == "-" && x->a && stripParens(x->a)->k == EK::Num;
          case EK::Cond: return freshLit(x->b) && freshLit(x->c);
          case EK::Ident: return x->var && x->var->litInferred;
          default: return false;
        }
      };
      std::function<void(Stmt*)> scan = [&](Stmt* st) {
        if (!st || !fresh) return;
        if (st->k == SK::Return && st->expr) fresh = fresh && freshLit(st->expr);
        for (Stmt* y : st->stmts) scan(y);
        scan(st->then); scan(st->els); scan(st->body);
        for (auto& c : st->cases) for (Stmt* y : c.body) scan(y);
      };
      if (f->exprBody) fresh = freshLit(f->exprBody);
      else scan(f->body);
      if (fresh) r = widen(r);
    }
    if (bare && valued && r && r->t != TT::Unknown) {
      r = T.optional(r, false, true);
      // the returned values were checked before the type was known: convert them to it
      std::function<void(Stmt*)> conv = [&](Stmt* s) {
        if (!s) return;
        switch (s->k) {
          case SK::Return:
            if (s->expr && s->expr->type && s->expr->type->t != TT::Promise) coerce(s->expr, f->isAsync ? r : r, "return");
            break;
          case SK::Block: for (Stmt* x : s->stmts) conv(x); break;
          case SK::If: conv(s->then); conv(s->els); break;
          case SK::While: case SK::DoWhile: case SK::For: case SK::ForOf: case SK::Labeled: conv(s->body); break;
          case SK::Try: conv(s->body); conv(s->els); conv(s->then); break;
          case SK::Switch: for (auto& c : s->cases) for (Stmt* x : c.body) conv(x); break;
          default: break;
        }
      };
      if (f->body) conv(f->body);
    }
    if (!r) r = T.voidT();
    if ((f->isGenerator || f->isAsync) && r->t == TT::Undefined) r = T.voidT();
    if (r->t == TT::Null || r->t == TT::Undefined) {
      err(f->loc, "cannot infer return type of '" + f->name + "' from null/undefined; add a return type annotation");
      r = T.unknown();
    }
    // a function returning values on some paths only
    if (r->t != TT::Void && f->body && !exits(f->body)) {
      err(f->loc, "not all code paths of '" + f->name + "' return a value");
    }
    if (f->isAsync && !f->isGenerator) r = T.promise(r->t == TT::Undefined ? T.voidT() : r);
    if (f->isGenerator) {
      Type* y = nullptr;
      for (Type* t : f->yieldTypes) y = y ? unify(y, t, f->loc, "yield expressions") : t;
      if (!y || y->t == TT::Void || y->t == TT::Undefined || y->t == TT::Never) y = T.undef();
      r = f->isAsync ? T.asyncGenerator(y, r, T.voidT(), 0) : T.generator(y, r, T.voidT(), 0);
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
    if (s->k != SK::Func || s->fn || s->generic) continue;
    if (!s->typeParams.empty()) {
      // a nested generic function: its instances are closures created where it is declared
      GenericSym* g = newGeneric(GenericSym::Func, s->name, &s->typeParams, s->loc);
      g->decl = s;
      g->parentFn = curFn;
      g->topLevel = false;
      s->generic = g;
      declare(s->name, g, s->loc);
      continue;
    }
    FuncSym* f = newFunc(s->name, &s->params, s->retType, s->body, nullptr);
    f->isGenerator = s->isGenerator;
    f->isAsync = s->isAsync;
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
  // the let / const names of the block: a closure before the declaration refers to them (not to an outer
  // variable of the same name)
  if (scope && scope != globalScope && !(curFn && curFn->isInit && scope->parent == globalScope)) {
    std::function<void(VarDecl&)> names = [&](VarDecl& d) {
      if (!d.isArrayPat && !d.isObjPat) { if (!d.name.empty() && !scope->names.count(d.name)) scope->pending.insert(d.name); }
      for (auto& sub : d.elems) names(sub);
    };
    for (Stmt* s : stmts)
      if (s->k == SK::Var)
        for (auto& d : s->decls) names(d);
  }
  hoistFunctions(stmts);
  // const f = (x: T): R => ... in a function's block: declared on entry, so that the function values
  // before it may call it (const isEven = ... isOdd(k - 1); const isOdd = ...), as in JavaScript
  bool topLevel = curFn && curFn->isInit && scope && scope->parent == globalScope;
  if (curFn && !topLevel)
    for (Stmt* s : stmts)
      if (s->k == SK::Var && s->isConst)
        for (auto& d : s->decls) {
          if (d.isArrayPat || d.isObjPat || !d.init || d.name.empty() || d.generic || scope->names.count(d.name)) continue;
          Expr* ai = d.init;
          while (ai->k == EK::Paren) ai = ai->a;
          if (ai->k != EK::Arrow || !ai->typeParams.empty()) continue;
          Type* ft = knownFnType(d, d.type ? resolveType(d.type) : nullptr);
          if (!ft) continue;
          bindPattern(d, ft, true, false, d.loc);
          if (d.sym) d.sym->tdzLocal = true;
          preBound.insert(&d);
        }
  for (Stmt* s : stmts) checkStmt(s);
}

// the type of `const f = <arrow>` known without checking the arrow: an annotation, or typed parameters
// and a return type (null otherwise)
Type* Checker::knownFnType(VarDecl& d, Type* declared) {
  Expr* ai = d.init;
  while (ai && ai->k == EK::Paren) ai = ai->a;
  if (!ai || ai->k != EK::Arrow || !ai->typeParams.empty()) return nullptr;
  if (declared) return declared->t == TT::Func ? declared : nullptr;
  if (!ai->retType) return nullptr;
  std::vector<Type*> ps;
  std::vector<bool> opt;
  bool rest = false;
  for (auto& p : ai->params) {
    if (!p.type || p.init || p.pattern) return nullptr;
    Type* pt = resolveType(p.type);
    if (p.optional) pt = T.optional(pt, false, true);
    ps.push_back(pt);
    opt.push_back(p.optional);
    rest = rest || p.rest;
  }
  return T.func(ps, resolveType(ai->retType), opt, rest);
}

// a switch over an enum value whose cases name every member of the enum (as tsc, exhaustive)
static Type* nullableCovered(Stmt* s, Type* dt);

bool Checker::switchCoversLits(Stmt* s) {
  Type* dt = nullableCovered(s, s->expr ? s->expr->type : nullptr);
  if (dt && dt->t == TT::Bool && !dt->lit) {
    // switch (b) { case true: ...; case false: ... }: both values (their union is boolean itself)
    Type* covered = nullptr;
    for (auto& c : s->cases)
      if (c.test)
        if (Type* lt = constLit(c.test); lt && lt->t == TT::Bool) covered = covered ? T.litUnion(covered, lt) : lt;
    return covered && !covered->lit;
  }
  if (!dt || !dt->lit) return false;
  Type* covered = nullptr;
  for (auto& c : s->cases)
    if (c.test)
      if (Type* lt = constLit(c.test)) covered = covered ? T.litUnion(covered, lt) : lt;
  return covered && litMinus(dt, covered) == nullptr;
}

// whether a switch clause's body ends by leaving the switch (no fall-through into the next clause)
bool Checker::exitsClause(SwitchCase& c) {
  for (Stmt* x : c.body)
    if (exits(x) || x->k == SK::Break) return true;
  return false;
}

// the values of literal type a that are not in b (null: none left)
Type* Checker::litMinus(Type* a, Type* b) {
  if (!b) return a;
  if (a->t == TT::String) {
    std::vector<std::u16string> v;
    for (auto& s : a->strLits)
      if (!std::binary_search(b->strLits.begin(), b->strLits.end(), s)) v.push_back(s);
    return v.empty() ? nullptr : T.strLit(v);
  }
  if (a->t == TT::Number) {
    std::vector<double> v;
    for (double d : a->numLits) {
      bool in = false;
      for (double e : b->numLits) in = in || e == d;
      if (!in) v.push_back(d);
    }
    return v.empty() ? nullptr : T.numLit(v);
  }
  uint8_t m = a->boolLits & ~b->boolLits;
  return m ? T.boolLit(m) : nullptr;
}

// a switch over a nullable value: its cases include null / undefined for each it may be (null: not covered)
static Type* nullableCovered(Stmt* s, Type* dt) {
  if (!dt || dt->t != TT::Optional) return dt;
  bool n = false, u = false;
  for (auto& c : s->cases) {
    if (!c.test) continue;
    Expr* x = stripParens(c.test);
    n = n || x->k == EK::Null;
    u = u || x->k == EK::Undefined;
  }
  if ((dt->hasNull && !n) || (dt->hasUndef && !u)) return nullptr;
  return dt->elem;
}

bool Checker::switchCoversEnum(Stmt* s) {
  Type* dt = nullableCovered(s, s->expr ? s->expr->type : nullptr);
  if (!dt || dt->t != TT::Enum || !dt->en) return false;
  std::vector<double> nums;
  std::vector<std::u16string> strs;
  for (auto& c : s->cases) {
    if (!c.test) continue;
    Expr* x = c.test;
    while (x->k == EK::Paren) x = x->a;
    if (x->type != dt) continue;
    if (x->intrinsic == IN_CONST_NUM) nums.push_back(x->num);
    else if (x->intrinsic == IN_CONST_STR) strs.push_back(x->str);
  }
  for (auto& m : dt->en->members) {
    bool found = dt->en->isString ? std::find(strs.begin(), strs.end(), m.str) != strs.end()
                                  : std::find(nums.begin(), nums.end(), m.num) != nums.end();
    if (!found) return false;
  }
  return true;
}

bool Checker::exits(Stmt* s) {
  if (!s) return false;
  switch (s->k) {
    case SK::Return: case SK::Throw: case SK::Break: case SK::Continue: return true;
    case SK::Block:
      for (Stmt* x : s->stmts) if (exits(x)) return true;
      return false;
    case SK::If: return s->els && exits(s->then) && exits(s->els);
    case SK::Try:
      if (s->then && exits(s->then)) return true;
      return exits(s->body) && (!s->els || exits(s->els));
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
      if (!hasDefault && !switchCoversEnum(s) && !switchCoversLits(s)) return false;  // an enum / literal switch with every value is exhaustive
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

// a break or continue anywhere in s (also of inner loops: a conservative answer)
static bool hasJump(Stmt* s) {
  if (!s) return false;
  switch (s->k) {
    case SK::Break: case SK::Continue: return true;
    case SK::Block:
      for (Stmt* x : s->stmts) if (hasJump(x)) return true;
      return false;
    case SK::If: return hasJump(s->then) || hasJump(s->els);
    case SK::Try: return hasJump(s->body) || hasJump(s->els) || hasJump(s->then);
    case SK::While: case SK::DoWhile: case SK::For: case SK::ForOf: case SK::Labeled: return hasJump(s->body);
    case SK::Switch:
      for (auto& c : s->cases)
        for (Stmt* x : c.body) if (hasJump(x)) return true;
      return false;
    default: return false;
  }
}

// a `break label` anywhere in s
static bool hasLabeledBreak(Stmt* s) {
  if (!s) return false;
  switch (s->k) {
    case SK::Break: return !s->label.empty();
    case SK::Block:
      for (Stmt* x : s->stmts) if (hasLabeledBreak(x)) return true;
      return false;
    case SK::If: return hasLabeledBreak(s->then) || hasLabeledBreak(s->els);
    case SK::Try: return hasLabeledBreak(s->body) || hasLabeledBreak(s->els) || hasLabeledBreak(s->then);
    case SK::While: case SK::DoWhile: case SK::For: case SK::ForOf: case SK::Labeled: return hasLabeledBreak(s->body);
    case SK::Switch:
      for (auto& c : s->cases)
        for (Stmt* x : c.body) if (hasLabeledBreak(x)) return true;
      return false;
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
    case SK::Try:
      return containsBreakFor(s->body, label, depth) || containsBreakFor(s->els, label, depth) ||
             containsBreakFor(s->then, label, depth);
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

// Narrowing by assignment that ends a branch: after `if (x === null) x = [];` (or `x ??= []`, or an else
// branch that assigns too) x is non-null on both paths, as in tsc. A branch that does not assign the path
// keeps what its condition says about it.
Type* Checker::branchEndNarrow(Stmt* b, std::string& key) {
  // { x = new X(); m.set(k, x); }: the last assignment of a block, when no later statement of the block
  // assigns its target again
  if (b && b->k == SK::Block) {
    for (size_t i = b->stmts.size(); i-- > 0;) {
      std::string k;
      Type* t = branchEndNarrow(b->stmts[i], k);
      if (!t) continue;
      for (size_t j = i + 1; j < b->stmts.size(); j++)
        if (assignsPath(b->stmts[j], k)) return nullptr;
      key = k;
      return t;
    }
    return nullptr;
  }
  if (!b || b->k != SK::Expr || !b->expr || b->expr->k != EK::Assign) return nullptr;
  Expr* a = b->expr;
  if (a->raw != "=" && a->raw != "??=") return nullptr;
  Type* lt = a->a->type;
  Expr* rhs = a->b;
  while (rhs && rhs->k == EK::Conv) rhs = rhs->a;  // the value before its conversion to the target type
  Type* rt = rhs ? rhs->type : nullptr;
  if (!lt || !rt || lt->t != TT::Optional) return nullptr;
  key = pathKey(a->a);
  if (key.empty()) return nullptr;
  if (a->raw == "??=") return lt->elem;
  if (!nonNullValue(a->b)) return nullptr;
  return lt->elem;
}

bool Checker::assignsPath(Stmt* b, const std::string& key) {
  std::set<std::string> names;
  collectAssigned(b, names);
  for (auto& n : names) {
    std::string root = n.substr(0, n.find('.'));
    Sym* s = lookup(root);
    if (!s || s->k != SymK::Var) continue;
    std::string k = varKey(static_cast<VarSym*>(s));
    if (n.size() > root.size()) k += n.substr(root.size());
    if (k == key || key.compare(0, k.size() + 1, k + ".") == 0) return true;
  }
  return false;
}

void Checker::joinBranchNarrowing(Stmt* s, const std::vector<Fact>& tf, const std::vector<Fact>& ff) {
  std::string k1, k2;
  Type* t1 = branchEndNarrow(s->then, k1);
  Type* t2 = s->els ? branchEndNarrow(s->els, k2) : nullptr;
  auto fact = [](const std::vector<Fact>& fs, const std::string& key) -> Type* {
    for (auto& f : fs) if (f.key == key) return f.t;
    return nullptr;
  };
  for (const std::string* kp : {&k1, &k2}) {
    const std::string& key = *kp;
    if (key.empty() || (kp == &k2 && key == k1)) continue;
    Type* a = key == k1 && t1 ? t1 : assignsPath(s->then, key) ? nullptr : fact(tf, key);
    Type* b = key == k2 && t2 ? t2 : s->els && assignsPath(s->els, key) ? nullptr : fact(ff, key);
    if (a && b && a == b && a->t != TT::Optional) setNarrow(key, a);
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
    v->type = inferVarType(d.init, t, v->isConst);
    v->litInferred = v->type->lit;
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
  // an element with a default: the value is used unless it is undefined, so the binding has the
  // element type without undefined; the default must fit that type
  auto withDefault = [&](VarDecl& el, Type* et) -> Type* {
    if (!el.init) return et;
    Type* bt = et;
    if (et->t == TT::Optional && et->hasUndef) bt = et->hasNull ? T.optional(et->elem, true, false) : et->elem;
    check(el.init, bt->t == TT::Unknown ? nullptr : bt);
    if (bt->t != TT::Unknown) coerce(el.init, bt, "default value");
    return bt;
  };
  if (d.isArrayPat) {
    // a generator (or an iterable class) is destructured like an array of its values
    if (t->t == TT::Generator) t = T.array(t->elem);
    else if (Type* gt = t->t == TT::Class ? iteratorMethodType(t, l) : nullptr) t = T.array(gt->elem);
    for (size_t i = 0; i < d.elems.size(); i++) {
      VarDecl& el = d.elems[i];
      Type* et;
      if (el.isRest) {
        // [a, ...rest]: the remaining elements (an array of the same type, or a tuple of the rest)
        if (t->t == TT::Array) et = t;
        else if (t->t == TT::Tuple) {
          std::vector<Type*> restElems(t->elems.begin() + (std::ptrdiff_t)std::min(i, t->elems.size()), t->elems.end());
          if (restElems.empty()) { err(el.loc, "the rest element would be an empty tuple"); et = T.unknown(); }
          else et = T.tuple(restElems);
        } else if (t->t == TT::Unknown) et = T.unknown();
        else { err(l, "cannot destructure a value of type '" + typeName(t) + "' as an array"); et = T.unknown(); }
        el.restType = et;
        bindPattern(el, et, isConst, topLevel, l);
        continue;
      }
      if (t->t == TT::Array) et = t->elem;
      else if (t->t == TT::Tuple) {
        if (i >= t->elems.size()) { err(d.loc, "tuple has no element at index " + std::to_string(i)); et = T.unknown(); }
        else et = t->elems[i];
      } else if (t->t == TT::Unknown) et = T.unknown();
      else { err(l, "cannot destructure a value of type '" + typeName(t) + "' as an array"); et = T.unknown(); }
      if (el.name.empty() && !el.isArrayPat && !el.isObjPat) continue;  // hole
      bindPattern(el, withDefault(el, et), isConst, topLevel, l);
    }
    return;
  }
  // object pattern
  std::vector<std::string> named;
  for (size_t i = 0; i < d.elems.size(); i++) {
    const std::string& key = d.keys[i];
    VarDecl& el = d.elems[i];
    if (el.isRest) {
      // { a, ...rest }: an object with the other (non-private) properties of the static type
      std::vector<ObjShape::Prop> props;
      if (t->t == TT::Class) {
        resolveClass(t->cls);
        for (auto& f : t->cls->fields)
          if (f.name[0] != '#' && std::find(named.begin(), named.end(), f.name) == named.end())
            props.push_back({f.name, f.type, f.optional, f.undefOk});
        if (!t->cls->abstractProps.empty())
          err(el.loc, "a rest property of an abstract class type with abstract properties is not supported");
      } else if (t->t == TT::Object && t->shape->ownAccessors) {
        // an object literal with accessors: its properties and getters (own properties), in its order
        std::vector<std::pair<std::string, Type*>> own;
        if (readableProps(t, el.loc, "a rest property", own))
          for (auto& p : own)
            if (std::find(named.begin(), named.end(), p.first) == named.end()) {
              const ObjShape::Prop* sp = t->shape->findProp(p.first);
              props.push_back(sp ? *sp : ObjShape::Prop{p.first, p.second, false});
            }
      } else if (t->t == TT::Object) {
        for (auto& p : t->shape->props)
          if (std::find(named.begin(), named.end(), p.name) == named.end()) props.push_back(p);
      } else if (t->t != TT::Unknown) {
        err(l, "cannot destructure a value of type '" + typeName(t) + "'");
      }
      Type* rt = t->t == TT::Unknown ? T.unknown() : T.anonObject(props);
      el.restType = rt;
      bindPattern(el, rt, isConst, topLevel, l);
      continue;
    }
    named.push_back(key);
    Type* ft = T.unknown();
    if (t->t == TT::Class) {
      auto it = t->cls->fieldIdx.find(key);
      int gs = t->cls->getterSlot(key);
      if (it != t->cls->fieldIdx.end() && key[0] != '#' && !t->cls->fields[it->second].absent) {
        ft = t->cls->fields[it->second].type;
      } else if (gs >= 0) {
        // a getter: called while destructuring
        FuncSym* g = t->cls->vtable[gs].fn;
        resolveSig(g);
        if (g->inferRet && !g->checking) checkFunction(g);
        ft = g->type->ret;
      } else {
        err(d.loc, "property '" + key + "' does not exist on type '" + typeName(t) + "'");
      }
    } else if (t->t == TT::Object) {
      const ObjShape::Prop* p = t->shape->findProp(key);
      const ObjShape::Method* g = p ? nullptr : t->shape->findMethod("get " + key);
      if (p) ft = p->type;
      else if (g) ft = g->fn->ret;  // a getter (declared by an interface or an object literal): called
      else if (t->shape->findMethod("set " + key)) err(d.loc, "property '" + key + "' has no getter");
      else err(d.loc, "property '" + key + "' does not exist on type '" + typeName(t) + "'");
    } else if (t->t != TT::Unknown) {
      err(l, "cannot destructure a value of type '" + typeName(t) + "'");
    }
    bindPattern(el, withDefault(el, ft), isConst, topLevel, l);
  }
}

// `const [a, b] = [1, "x"]`: like tsc, an array literal destructured by an array pattern is a tuple
// when its elements have different types (`[number, string]`); with compatible element types it stays
// an array (a default can then apply past its end). Nested patterns and literals work the same way.
Type* Checker::checkArrayPatInit(VarDecl& d, Expr* init) {
  auto strip = [](Expr* e) {
    while (e && e->k == EK::Paren) e = e->a;
    return e;
  };
  std::function<bool(Type*, Type*)> unifiable = [&](Type* a, Type* b) {  // unify() would not fail
    if (a == b) return true;
    auto loose = [](Type* t) {
      return t->t == TT::Never || t->t == TT::Unknown || t->t == TT::Null || t->t == TT::Undefined;
    };
    if (loose(a) || loose(b)) return true;
    if (a->t == TT::Optional || b->t == TT::Optional) return unifiable(baseOf(a), baseOf(b));
    if (a->t == TT::Class && b->t == TT::Class)
      for (ClassSym* c = a->cls; c; c = c->parent)
        if (isAncestor(c, b->cls)) return true;
    return assignable(a, b) || assignable(b, a);
  };
  Expr* lit = strip(init);
  if (!d.isArrayPat || lit->k != EK::Array || lit->list.empty()) return check(init, nullptr);
  for (Expr* x : lit->list) {
    Expr* sx = strip(x);
    // spreads, and empty array elements (typed from the other elements): an array, as before
    if (x->k == EK::Spread || (sx->k == EK::Array && sx->list.empty())) return check(init, nullptr);
  }
  std::vector<Type*> ts;
  for (size_t i = 0; i < lit->list.size(); i++) {
    Expr* x = lit->list[i];
    VarDecl* sub = i < d.elems.size() && !d.elems[i].isRest ? &d.elems[i] : nullptr;
    Type* t = sub && sub->isArrayPat ? checkArrayPatInit(*sub, x) : check(x, nullptr);
    ts.push_back(t);
  }
  bool same = true;
  for (Type* t : ts) {
    if (t->t == TT::Null || t->t == TT::Undefined || t->t == TT::Void || t->t == TT::Unknown) return check(init, nullptr);
    same = same && unifiable(ts[0], t);
  }
  // the elements are checked already (checkArray keeps their types)
  return check(init, same ? nullptr : T.tuple(ts));
}

void Checker::checkVarDecl(Stmt* s, VarDecl& d, bool isConst, bool topLevel) {
  bool simple = !d.isArrayPat && !d.isObjPat;
  if (d.isObjPat && !d.elems.empty() && d.elems.back().isRest && d.init && stripParens(d.init)->k == EK::Cond) {
    // const { a, ...rest } = c ? x : y: from the copy { ...(c ? x : y) } (the properties of the branch taken)
    Expr* lit = P.arena.expr(EK::Object, d.init->loc);
    ObjProp sp;
    sp.loc = d.init->loc;
    sp.spread = true;
    sp.value = d.init;
    lit->props.push_back(sp);
    d.init = lit;
  }
  if (d.generic) return;  // top-level `const f = <T>(...) => ...` (see declareFile)
  if (simple && d.init && !d.type) {
    Expr* gi = d.init;
    while (gi->k == EK::Paren) gi = gi->a;
    if (gi->k == EK::Arrow && !gi->typeParams.empty()) {
      // const f = <T>(x: T) => ...: a generic function; its instances are closures created here
      if (!isConst) { err(d.loc, "a generic arrow function must be assigned to a 'const'"); return; }
      GenericSym* g = newGeneric(GenericSym::Arrow, d.name, &gi->typeParams, d.loc);
      g->arrow = gi;
      g->parentFn = curFn;
      g->topLevel = curFn == nullptr || (curFn->isInit && topLevel);
      d.generic = g;
      declare(d.name, g, d.loc);
      return;
    }
  }
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
        declared = inferVarType(d.init, t, isConst);
      }
    } else if (!declared) {
      err(d.loc, "'" + d.name + "' needs a type annotation or initializer");
      declared = T.unknown();
    } else if (isConst) {
      err(d.loc, "const declarations must be initialized");
    }
    v->type = declared;
    v->checked = true;
    v->litInferred = !d.type && declared->lit;
    narrowDeclInit(d);
    noteConstArrow(d, isConst);
    return;
  }
  Type* declared = d.type ? resolveType(d.type) : nullptr;
  // a local function value that calls itself (const f = (n: number): number => ... f(n - 1)): when its type
  // is known without checking the initializer (an annotation, or an arrow with typed parameters and a return
  // type), the variable is in scope in the initializer; the closure captures it in a cell, as in JavaScript
  if (simple && !topLevel && d.init && !d.name.empty()) {
    Type* ft = knownFnType(d, declared);
    if (ft) {
      // (declared already when the block was entered: the functions before it may call it)
      if (!preBound.erase(&d)) bindPattern(d, ft, isConst, false, d.loc);
      check(d.init, ft);
      coerce(d.init, ft);
      noteConstArrow(d, isConst);
      return;
    }
  }
  Type* t = declared;
  if (d.init) {
    Type* it = !declared && d.isArrayPat ? checkArrayPatInit(d, d.init) : check(d.init, declared);
    if (d.isArrayPat && !declared && (it->t == TT::Set || it->t == TT::Map || it->t == TT::String ||
                                      it->t == TT::Bytes || it->t == TT::TypedArray)) {
      // const [a, b] = new Set(...) / "text": the values it iterates, as an array [...x]
      Expr* sp = P.arena.expr(EK::Spread, d.init->loc);
      sp->a = wrapConv(d.init, ConvKind::None, it);  // (checked already: not checked again)
      Expr* arr = P.arena.expr(EK::Array, d.init->loc);
      arr->list.push_back(sp);
      d.init = arr;
      it = check(d.init, nullptr);
    }
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
      t = simple ? inferVarType(d.init, it, isConst) : widen(it);
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
  if (simple && d.sym) d.sym->litInferred = !declared && t && t->lit;
  if (simple) narrowDeclInit(d);
  noteConstArrow(d, isConst);
  (void)s;
}

// const isK = (k: string): k is Kind => ...: calls of isK narrow as calls of a function isK do
void Checker::noteConstArrow(VarDecl& d, bool isConst) {
  if (!isConst || !d.sym || !d.init) return;
  Expr* x = d.init;
  while (x && (x->k == EK::Paren || x->k == EK::Conv)) x = x->a;
  if (x && x->k == EK::Arrow && x->fn) constArrowFns[d.sym] = x->fn;
}

FuncSym* Checker::predCallee(Expr* c) {
  if (c->callee) return c->callee;
  Expr* f = c->a;
  while (f && f->k == EK::Paren) f = f->a;
  if (!f || f->k != EK::Ident || !f->var) return nullptr;
  auto it = constArrowFns.find(f->var);
  if (it != constArrowFns.end()) return it->second;
  VarSym* v = f->var;  // a top-level const (typed lazily)
  if (!v->isConst || !v->decl || !v->decl->init) return nullptr;
  Expr* x = v->decl->init;
  while (x && (x->k == EK::Paren || x->k == EK::Conv)) x = x->a;
  return x && x->k == EK::Arrow ? x->fn : nullptr;
}

// let x: T | null = <non-null value>: x is narrowed to T afterwards (as an assignment narrows it)
void Checker::narrowDeclInit(VarDecl& d) {
  VarSym* v = d.sym;
  if (!v || !d.init || !v->type || v->type->t != TT::Optional || !nonNullValue(d.init)) return;
  setNarrow(varKey(v), v->type->elem);
}

// ------------------------------------------------------------------ statements
void Checker::checkStmt(Stmt* s) {
  bool topLevel = curFn && curFn->isInit && scope && scope->parent == globalScope;
  switch (s->k) {
    case SK::Var:
      for (auto& d : s->decls) {
        checkVarDecl(s, d, s->isConst, topLevel);
        std::function<void(VarDecl&)> mark = [&](VarDecl& x) {
          if (x.sym && !x.sym->isGlobal) x.sym->blockVar = true;
          for (auto& sub : x.elems) mark(sub);
        };
        mark(d);
      }
      break;
    case SK::Func:
      if (topLevel) break;  // top-level functions are checked separately
      if (!s->fn && !s->generic) { std::vector<Stmt*> one{s}; hoistFunctions(one); }
      if (s->generic) break;  // a generic function: its instances are checked where they are created
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
        // static field initializers and static blocks run here, in source order, inside the class
        // body (private names of the class are accessible)
        ClassSym* savedStatic = staticInitClass;
        staticInitClass = s->cls;
        for (auto& m : s->members) {
          if (m.kind == ClassMember::StaticBlock) {
            pushScope();
            checkBlockStmts(m.body->stmts);
            popScope();
            continue;
          }
          if (m.kind != ClassMember::Field || !m.isStatic) continue;
          auto it = s->cls->staticFields.find(m.name);
          if (it == s->cls->staticFields.end()) continue;
          VarSym* v = it->second;
          VarDecl* d = v->decl;
          if (d && d->init && !v->checked) {
            Type* t = check(d->init, v->type);
            if (v->type) coerce(d->init, v->type);
            else v->type = t;
          }
          if (!v->type) { err(v->loc, "static field '" + m.name + "' needs a type annotation or initializer"); v->type = T.unknown(); }
          v->checked = true;
        }
        staticInitClass = savedStatic;
      }
      break;
    case SK::Interface: case SK::TypeAlias: case SK::Enum:
      if (!topLevel) err(s->loc, "type declarations must be at top level");
      break;
    case SK::Import: case SK::Export: case SK::Empty: case SK::Debugger:
      break;
    case SK::Expr: {
      // x++ / x-- as a statement (its value unused) of a nullable x narrowed to a number, or of x!: x += 1
      // (compound assignment handles narrowed and non-null targets)
      if (Expr* u = s->expr; u->k == EK::Update && u->a) {
        Expr* tgt = stripParens(u->a);
        bool path = tgt->k == EK::Ident || tgt->k == EK::Member;
        for (Expr* x = tgt; path && x->k == EK::Member; x = stripParens(x->a))
          path = !x->optional && x->a && (stripParens(x->a)->k == EK::Ident || stripParens(x->a)->k == EK::This ||
                                          stripParens(x->a)->k == EK::Member);
        Type* dt = nullptr;
        if (tgt->k == EK::NonNull && tgt->a) {
          dt = check(cloneExpr(tgt), nullptr);  // (a copy: the target itself is checked with the assignment)
        } else if (path) {
          Expr* probe = cloneExpr(tgt);
          dt = check(probe, nullptr);
          // the declared type of the target (a nullable one only)
          Type* st = nullptr;
          if (probe->k == EK::Ident && probe->var) st = probe->var->type;
          else if (probe->k == EK::Member) {
            if (probe->var && probe->intrinsic == IN_STATIC_FIELD) st = probe->var->type;
            else if (probe->cls && probe->field >= 0 && (size_t)probe->field < probe->cls->fields.size())
              st = probe->cls->fields[(size_t)probe->field].type;
            else if (Type* ot = probe->a && probe->a->type ? baseOf(probe->a->type) : nullptr; ot && ot->t == TT::Object)
              if (const ObjShape::Prop* pp = ot->shape->findProp(probe->raw)) st = pp->type;
          }
          if (!st || st->t != TT::Optional) dt = nullptr;
        }
        if (dt && (dt->t == TT::Number || dt->t == TT::BigInt) && !dt->lit) {
          bool big = dt->t == TT::BigInt;
          Expr* one = P.arena.expr(big ? EK::BigLit : EK::Num, u->loc);
          one->raw = "1";
          one->num = 1;
          one->ival = 1;
          one->isIntLit = !big;
          u->k = EK::Assign;
          u->raw = u->raw == "++" ? "+=" : "-=";
          u->b = one;
        }
      }
      Expr* savedStmtExpr = stmtExpr_;
      stmtExpr_ = s->expr;  // (its value is not used)
      while (stmtExpr_->k == EK::Paren) stmtExpr_ = stmtExpr_->a;
      Type* t = check(s->expr, nullptr);
      stmtExpr_ = savedStmtExpr;
      (void)t;
      // an assertion function: the rest of the block sees what it asserts
      Expr* c = s->expr;
      while (c->k == EK::Paren) c = c->a;
      FuncSym* pf = c->k == EK::Call ? predCallee(c) : nullptr;
      if (pf && pf->asserts && pf->predParam >= 0 && (size_t)pf->predParam < c->list.size()) {
        Expr* arg = c->list[(size_t)pf->predParam];
        while (arg->k == EK::Conv || arg->k == EK::Paren) arg = arg->a;
        std::vector<Fact> facts;
        if (pf->predType) {
          std::string key = pathKey(arg);
          if (!key.empty()) facts.push_back({key, pf->predType});
        } else {
          factsFrom(arg, true, facts);
        }
        applyFacts(facts);
      }
      break;
    }
    case SK::Block: {
      Scope* inner = pushScope();
      checkBlockStmts(s->stmts);
      popScope();
      // a block that ends normally: what is narrowed at its end holds after it (a labeled break out of
      // the block skips its end)
      if (!hasLabeledBreak(s)) keepNarrowing(inner);
      break;
    }
    case SK::If: {
      checkCond(s->expr);
      std::vector<Fact> tf, ff;
      factsFrom(s->expr, true, tf);
      factsFrom(s->expr, false, ff);
      Scope* thenScope = pushScope();
      applyFacts(tf);
      checkStmt(s->then);
      popScope();
      Scope* elsScope = nullptr;
      if (s->els) {
        elsScope = pushScope();
        applyFacts(ff);
        checkStmt(s->els);
        popScope();
      }
      // when one branch always exits, the code after the if runs only after the other branch: what is
      // narrowed at its end holds (if (a instanceof B) return; else if (a === null) return; a.name)
      bool te = exits(s->then), ee = s->els && exits(s->els);
      if (te && !ee) { if (elsScope) keepNarrowing(elsScope); else applyFacts(ff); }
      else if (ee && !te) keepNarrowing(thenScope);
      else if (!te && !ee) {
        joinBranchNarrowing(s, tf, ff);
        // a path both branches narrow to the same type at their ends (nested ifs that assign it too)
        std::set<std::string> keys;
        for (auto& kv : thenScope->narrow) keys.insert(kv.first);
        if (elsScope) for (auto& kv : elsScope->narrow) keys.insert(kv.first);
        for (auto& k : keys) {
          auto at = [&](Scope* sc, const std::vector<Fact>* facts) -> Type* {
            if (sc) {
              auto it = sc->narrow.find(k);
              if (it != sc->narrow.end()) return it->second;
            } else if (facts) {
              for (auto& f : *facts) if (f.key == k) return f.t;
            }
            return narrowed(k, false);
          };
          Type* a = at(thenScope, nullptr);
          Type* b = elsScope ? at(elsScope, nullptr) : at(nullptr, &ff);
          if (a && a == b && a->t != TT::Optional && narrowed(k, false) != a) setNarrow(k, a);
        }
      }
      break;
    }
    case SK::While: {
      invalidateAssigned(s->body, s->expr);
      checkCond(s->expr);
      std::vector<Fact> tf, ff;
      factsFrom(s->expr, true, tf);
      factsFrom(s->expr, false, ff);
      pushScope();
      applyFacts(tf);
      std::string label = pendingLabels.empty() ? "" : pendingLabels.back();
      loops.push_back({label, true, false});
      pendingLabels.clear();
      checkStmt(s->body);
      loops.pop_back();
      popScope();
      // the loop ends when its condition is false (unless a break leaves it): while (x === null) x = ...
      if (!containsBreakFor(s->body, "", 0) && (label.empty() || !containsBreakFor(s->body, label, 0)))
        applyFacts(ff);
      break;
    }
    case SK::DoWhile: {
      invalidateAssigned(s->body, s->expr);
      std::string label = pendingLabels.empty() ? "" : pendingLabels.back();
      loops.push_back({label, true, false});
      pendingLabels.clear();
      Scope* body = pushScope();
      checkStmt(s->body);
      popScope();
      loops.pop_back();
      // the condition runs after the body's end (unless a continue skips to it)
      bool jumps = hasJump(s->body);
      Scope* condScope = pushScope();
      if (!jumps) keepNarrowing(body);
      checkCond(s->expr);
      std::vector<Fact> ff;
      factsFrom(s->expr, false, ff);
      popScope();
      if (!jumps) {
        keepNarrowing(condScope);
        applyFacts(ff);
      }
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
          // t.entries() of a readonly tuple whose elements have one type: of an array of its elements
          if (ot->t == TT::Tuple && ot->ro)
            if (Expr* arr = tupleAsArray(ie->a->a, ot)) { ie->a->a = arr; ot = arr->type; }
          const std::string& mn = ie->a->raw;
          if (ot->t == TT::Array) {
            s->declareKind = mn == "entries" ? 1 : mn == "keys" ? 2 : 3;
            et = mn == "entries" ? T.tuple({T.number(), ot->elem}) : mn == "keys" ? T.number() : ot->elem;
          } else if (ot->t == TT::Map) {
            s->declareKind = mn == "keys" ? 4 : mn == "values" ? 5 : 6;
            et = mn == "keys" ? ot->key : mn == "values" ? ot->val : T.tuple({ot->key, ot->val});
          } else if (ot->t == TT::TypedArray || ot->t == TT::Bytes) {
            // (over its elements as an array: codegen)
            Type* el = ot->t == TT::Bytes ? T.number() : taElem(ot);
            s->declareKind = mn == "entries" ? 1 : mn == "keys" ? 2 : 3;
            et = mn == "entries" ? T.tuple({T.number(), el}) : mn == "keys" ? T.number() : el;
          } else if (ot->t == TT::Set) {
            s->declareKind = 7;
            et = mn == "entries" ? T.tuple({ot->elem, ot->elem}) : ot->elem;
            if (mn == "entries") s->declareKind = 8;
          }
          if (s->declareKind) s->expr = ie->a->a;
        }
      }
      bool asyncIter = false;
      if (!et) {
        Type* it = check(s->expr, nullptr);
        Type* ag = it->t == TT::AsyncGenerator ? it : asyncIteratorMethodType(it, s->expr->loc);
        if (ag && s->isAwait) {
          // for await over an async iterable: its values are not awaited again
          et = ag->elem;
          asyncIter = true;
        } else if (ag && it->t == TT::AsyncGenerator) {
          err(s->expr->loc, "type '" + typeName(it) + "' is an async iterable: use 'for await'");
          et = T.unknown();
        } else {
          // a tuple whose elements have one type: over a copy of them as an array
          // (a tuple whose elements have one type: its elements, read live by codegen)
          if (Type* te = it->t == TT::Tuple ? tupleElemType(it) : nullptr) et = te;
          else et = elemTypeForIter(s->expr, it, s->expr->loc);
        }
      }
      if (s->isAwait && asyncIter) {
        if (!curFn || (!curFn->isAsync && !curFn->isInit))
          err(s->loc, "'for await' is only allowed in async functions and at the top level of the entry module");
        else if (curFn->isInit && !curFn->isAsync) {
          curFn->isAsync = true;
          curFn->type = T.func({}, T.promise(T.voidT()));
        }
      } else if (s->isAwait) {
        // for await: each element is awaited (a promise gives its value)
        if (!curFn || (!curFn->isAsync && !curFn->isInit))
          err(s->loc, "'for await' is only allowed in async functions and at the top level of the entry module");
        else if (curFn->isInit && !curFn->isAsync) {
          curFn->isAsync = true;
          curFn->type = T.func({}, T.promise(T.voidT()));
        }
        if (s->declareKind) err(s->loc, "'for await' over keys / values / entries is not supported; iterate the array");
        if (et->t == TT::Promise) et = et->elem;
        if (et->t == TT::Void) et = T.undef();
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
      Type* declared = f->inferRet ? nullptr : bodyRet(f);
      // ({ id }) => log(id) where a void function is expected: the expression body is evaluated for its
      // effects, as without the destructuring
      if (s->expr && s->fromExprBody && declared && declared->t == TT::Void) {
        s->k = SK::Expr;
        Type* t = check(s->expr, nullptr);
        (void)t;
        break;
      }
      if (s->expr) {
        Type* t = check(s->expr, declared);
        if (f->isAsync && t->t == TT::Promise) {
          // `return p` in an async function: its promise adopts p (codegen: RET_R of the promise)
          if (declared && t->elem != declared && !(declared->t == TT::Void && t->elem->t == TT::Void))
            err(s->loc, "type '" + typeName(t) + "' is not assignable to the return type 'Promise<" + typeName(declared) + ">'");
          if (!declared) f->returnTypes.push_back(t->elem);
        } else if (declared) {
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
      // a: b: <statement>: the labels name the same statement; `break a` / `continue a` use b
      while (s->body->k == SK::Labeled) {
        std::function<void(Stmt*)> rename = [&](Stmt* x) {
          if (!x) return;
          switch (x->k) {
            case SK::Break: case SK::Continue: if (x->label == s->label) x->label = s->body->label; break;
            case SK::Block: for (Stmt* y : x->stmts) rename(y); break;
            case SK::If: rename(x->then); rename(x->els); break;
            case SK::While: case SK::DoWhile: case SK::For: case SK::ForOf: rename(x->body); break;
            case SK::Labeled: if (x->label != s->label) rename(x->body); break;
            case SK::Try: rename(x->body); rename(x->els); rename(x->then); break;
            case SK::Switch: for (auto& c : x->cases) for (Stmt* y : c.body) rename(y); break;
            default: break;
          }
        };
        rename(s->body->body);
        s->label = s->body->label;
        s->body = s->body->body;
      }
      bool isLoop = s->body->k == SK::While || s->body->k == SK::DoWhile || s->body->k == SK::For || s->body->k == SK::ForOf;
      if (isLoop) {
        pendingLabels.push_back(s->label);
        checkStmt(s->body);
        pendingLabels.clear();
      } else {
        // lbl: if (...) { break lbl; } ...: a break out of the statement skips the rest of it, so what is
        // narrowed in it holds after it only when no break leaves it
        loops.push_back({s->label, false, false});
        Scope* inner = pushScope();
        checkStmt(s->body);
        popScope();
        loops.pop_back();
        if (!containsBreakFor(s->body, s->label, 0)) keepNarrowing(inner);
      }
      break;
    }
    case SK::Switch: {
      Type* dt0 = check(s->expr, nullptr);
      // a nullable discriminant (switch (op) with op: string | undefined): the clauses compare its value;
      // `case null:` / `case undefined:` match the absent value
      bool optDisc = dt0->t == TT::Optional;
      Type* dt = optDisc ? dt0->elem : dt0;
      if (dt->t != TT::Number && dt->t != TT::String && dt->t != TT::Int && dt->t != TT::Enum && dt->t != TT::Bool &&
          dt->t != TT::Unknown)
        err(s->expr->loc, "switch requires a number, string, boolean, integer or enum value (or one of them | null / undefined)");
      pushScope();
      loops.push_back({pendingLabels.empty() ? "" : pendingLabels.back(), false, true});
      pendingLabels.clear();
      int defaults = 0;
      std::vector<Stmt*> all;
      for (auto& c : s->cases)
        for (Stmt* x : c.body) all.push_back(x);
      // the clauses share one scope (as in JavaScript): their let / const names are pending in it
      {
        std::function<void(VarDecl&)> names = [&](VarDecl& d) {
          if (!d.isArrayPat && !d.isObjPat) { if (!d.name.empty() && !scope->names.count(d.name)) scope->pending.insert(d.name); }
          for (auto& sub : d.elems) names(sub);
        };
        for (Stmt* x : all)
          if (x->k == SK::Var)
            for (auto& d : x->decls) names(d);
      }
      hoistFunctions(all);
      // literal types (D5): each clause sees the discriminant narrowed to its case values (with the
      // values of the empty clauses that fall into it), the default clause the values left (never when
      // the switch covers them all)
      // (a string or number discriminant: each clause sees it as its case values, s: "a" | "b")
      bool plainDisc = !dt->lit && (dt->t == TT::String || dt->t == TT::Number);
      std::string dkey = optDisc || plainDisc || dt->lit || (dt->t == TT::Enum && dt->en) ? pathKey(s->expr) : "";
      Type* group = nullptr;
      bool groupOk = true;
      Type* covered = nullptr;
      bool coveredNull = false, coveredUndef = false;  // `case null:` / `case undefined:` clauses seen
      for (size_t ci = 0; ci < s->cases.size(); ci++) {
        auto& c = s->cases[ci];
        Type* cv = nullptr;
        if (c.test && optDisc && (stripParens(c.test)->k == EK::Null || stripParens(c.test)->k == EK::Undefined)) {
          check(c.test, dt0);
          Type* nt = stripParens(c.test)->k == EK::Null ? T.nullT() : T.undef();
          (nt->t == TT::Null ? coveredNull : coveredUndef) = true;
          if (!assignable(nt, dt0)) err(c.test->loc, "case type '" + typeName(nt) + "' is not comparable to '" + typeName(dt0) + "'");
          groupOk = false;  // the clause sees the discriminant as it is
        } else if (c.test) {
          Type* ct = check(c.test, dt);
          if (optDisc && ct->t == TT::Optional)
            err(c.test->loc, "a case value of a nullable switch must not be nullable itself ('" + typeName(ct) + "'); use null / undefined cases");
          if (dt->lit)
            if (Type* lt = constLit(c.test)) {
              ct = lt;
              cv = lt;
            }
          if (plainDisc)
            if (Type* lt = constLit(c.test); lt && lt->lit && baseLit(lt) == dt) cv = lt;
          if (ct != dt && !(dt->t == TT::Unknown || ct->t == TT::Unknown)) {
            bool ok = assignable(ct, dt) || (ct->lit && dt->lit && baseLit(ct) == baseLit(dt));
            if (!ok) err(c.test->loc, "case type '" + typeName(ct) + "' is not comparable to '" + typeName(dt) + "'");
          }
          if (cv && dt->lit) covered = covered ? T.litUnion(covered, cv) : cv;
          if (cv && groupOk) group = group ? T.litUnion(group, cv) : cv;
          else if (!(optDisc && !dt->lit)) groupOk = false;  // (a nullable non-literal one: narrowed to its base)
        } else {
          defaults++;
          groupOk = false;
        }
        pushScope();
        scope->declareUp = true;
        if (!dkey.empty() && optDisc) {
          // a clause reached only by its values sees them (not null / undefined); the default clause sees
          // the values left, or only null / undefined
          if (c.test && groupOk) setNarrow(dkey, group ? group : dt);
          else if (!c.test && (ci == 0 || exitsClause(s->cases[ci - 1]))) {
            // without the values of the clauses before it (case null: return ...; default: x is not null)
            Type* rest = dt->lit ? litMinus(dt, covered) : dt;
            bool n = dt0->hasNull && !coveredNull, u = dt0->hasUndef && !coveredUndef;
            if (rest) setNarrow(dkey, T.optional(rest, n, u));
            else if (n || u) setNarrow(dkey, T.optional(T.nullT(), n, u));
            else setNarrow(dkey, T.never());  // (every value and null / undefined have a clause)
          }
        } else if (!dkey.empty() && dt->lit) {
          if (c.test && groupOk && group) setNarrow(dkey, group);
          else if (!c.test && (ci == 0 || exitsClause(s->cases[ci - 1]))) {
            Type* rest = litMinus(dt, covered);
            setNarrow(dkey, rest ? rest : T.never());
          }
        } else if (!dkey.empty() && plainDisc) {
          if (c.test && groupOk && group) setNarrow(dkey, group);
        } else if (!dkey.empty() && !c.test && switchCoversEnum(s) && (ci == 0 || exitsClause(s->cases[ci - 1]))) {
          setNarrow(dkey, T.never());
        }
        for (Stmt* x : c.body) {
          checkStmt(x);
          // a let / const of a clause: another clause may run without its declaration (a ReferenceError
          // in JavaScript): its "initialized" flag is checked
          if (x->k == SK::Var) {
            std::function<void(VarDecl&)> flag = [&](VarDecl& d) {
              if (d.sym && !d.sym->isGlobal && !d.sym->initFlag) {
                d.sym->initFlag = P.make<VarSym>();
                d.sym->initFlag->name = d.sym->name + "$init";
                d.sym->initFlag->type = T.boolean();
                d.sym->initFlag->owner = d.sym->owner;
                d.sym->initFlag->isGlobal = false;
              }
              for (auto& sub : d.elems) flag(sub);
            };
            for (auto& d : x->decls) flag(d);
          }
        }
        popScope();
        if (!c.body.empty()) {
          group = nullptr;
          // the next clause is also reached from this one when its body falls through: it is not narrowed
          // to its own values then (case null: f(); case "x": ... sees null too)
          groupOk = exitsClause(c);
        }
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
      // A finally block runs after any prefix of the try block (and of the catch block), so it
      // starts with the narrowings of both dropped too.
      pushScope();
      checkStmt(s->body);
      popScope();
      invalidateAssigned(s->body);
      if (s->els) {
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
      }
      if (s->then) {
        pushScope();
        checkStmt(s->then);
        popScope();
        invalidateAssigned(s->then);
      }
      break;
    }
    case SK::ForIn:
      err(s->loc, "for...in is not supported");
      break;
  }
}

}  // namespace e7
