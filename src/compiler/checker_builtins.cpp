// Checker: calls, `new`, builtin library members.
#include <cmath>

#include "checker.h"
#include "intrinsics.h"
#include "suggest.h"

namespace e7 {

static Expr* strip(Expr* e) {
  while (e && e->k == EK::Paren) e = e->a;
  return e;
}

std::string Checker::kindSuffix(Type* t) { return std::string(1, kindChar(kindOf(t) == K_V ? K_R : kindOf(t))); }

static Expr* mkNum(Program& P, Types& T, double v, Loc l) {
  Expr* e = P.arena.expr(EK::Num, l);
  e->num = v;
  e->type = T.number();
  return e;
}
static Expr* mkStr(Program& P, Types& T, const std::u16string& s, Loc l) {
  Expr* e = P.arena.expr(EK::Str, l);
  e->str = s;
  e->type = T.string();
  return e;
}
static Expr* mkBool(Program& P, Types& T, bool b, Loc l) {
  Expr* e = P.arena.expr(EK::Bool, l);
  e->bval = b;
  e->type = T.boolean();
  return e;
}

bool Checker::argsMatch(Expr* call, std::vector<Expr*>& args, const std::vector<Type*>& params, size_t minArgs,
                        const char* fname, bool report) {
  for (Expr* a : args)
    if (a->k == EK::Spread) {
      if (report) err(a->loc, std::string("spread arguments are not supported for '") + fname + "'");
      return false;
    }
  if (args.size() < minArgs || args.size() > params.size()) {
    if (report) {
      std::string exp = minArgs == params.size() ? std::to_string(minArgs) : std::to_string(minArgs) + "-" + std::to_string(params.size());
      err(call->loc, std::string("'") + fname + "' expects " + exp + " argument(s), got " + std::to_string(args.size()));
    }
    return false;
  }
  bool ok = true;
  for (size_t i = 0; i < args.size(); i++) {
    Type* pt = params[i];
    Type* t = args[i]->type ? args[i]->type : check(args[i], pt);
    if (pt->t == TT::Unknown) continue;
    // a constant of a literal type's base: its value decides (m.set("n", 1) for a Map<"n" | "s", number>)
    if (baseOf(pt)->lit && !t->lit && (t->t == TT::String || t->t == TT::Number || t->t == TT::Bool))
      if (Type* c = constLit(args[i])) t = c;
    if (t != pt && !assignable(t, pt)) {
      if (report) err(args[i]->loc, "argument of type '" + typeName(t) + "' is not assignable to parameter of type '" + typeName(pt) + "'");
      ok = false;
      continue;
    }
    coerce(args[i], pt);
  }
  return ok;
}

// Check arguments against a function type (user functions / closures)
// f(...t) with a tuple t: one argument per element. The tuple is evaluated once, by the first
// element's node (it stores the tuple in a hidden local), the others read the hidden local.
void Checker::expandSpreads(std::vector<Expr*>& args) {
  for (size_t i = 0; i < args.size(); i++) {
    Expr* a = args[i];
    if (a->k != EK::Spread || a->type) continue;
    Expr* in = strip(a->a);
    if (in->k == EK::Array && in->list.empty()) {  // ...[] adds nothing
      args.erase(args.begin() + (std::ptrdiff_t)i);
      i--;
      continue;
    }
    Type* st = check(a->a, nullptr);
    a->type = st;
    iterUse(a->a);
    if (st->t != TT::Tuple) continue;
    VarSym* hv = newVar("<spread>", st, true, a->loc);
    hv->owner = curFn;
    std::vector<Expr*> elems;
    for (size_t k = 0; k < st->elems.size(); k++) {
      Expr* ix = P.arena.expr(EK::Index, a->loc);
      if (k == 0) {
        ix->a = a->a;
        ix->var = hv;
      } else {
        Expr* id = P.arena.expr(EK::Ident, a->loc);
        id->raw = "<spread>";
        id->var = hv;
        id->type = st;
        ix->a = id;
      }
      ix->field = (int)k;
      ix->cls = tupleRecord(st);
      ix->type = st->elems[k];
      elems.push_back(ix);
    }
    args.erase(args.begin() + (std::ptrdiff_t)i);
    args.insert(args.begin() + (std::ptrdiff_t)i, elems.begin(), elems.end());
    i += elems.size();
    i--;
  }
}

// Check arguments against a function type (user functions / closures)
static Expr* stripConvs(Expr* e) {
  while (e && (e->k == EK::Paren || (e->k == EK::Conv && e->conv == ConvKind::None))) e = e->a;
  return e;
}

// A copy of a parameter's default value when it is a constant (a literal or an enum member), which a
// call site can evaluate itself; null otherwise
Expr* Checker::constDefault(Expr* init) {
  Expr* x = init;
  while (x && (x->k == EK::Paren || (x->k == EK::Conv && x->conv == ConvKind::None))) x = x->a;
  if (!x) return nullptr;
  bool ok = x->k == EK::Str || x->k == EK::Num || x->k == EK::Bool || (x->k == EK::Template && x->list.empty()) ||
            (x->k == EK::Unary && x->raw == "-" && x->a && strip(x->a)->k == EK::Num);
  if (!ok && x->k == EK::Member && x->a && strip(x->a)->k == EK::Ident) {
    Sym* s = lookup(strip(x->a)->raw);
    ok = s && s->k == SymK::Enum;
  }
  if (!ok) return nullptr;
  Expr* d = P.arena.expr(x->k, x->loc);
  *d = *x;
  d->type = nullptr;
  return d;
}

void Checker::checkArgs(Expr* call, Type* ft, std::vector<Expr*>& args, size_t skip, FuncSym* fs) {
  (void)skip;
  if (!ft || ft->t != TT::Func) return;
  expandSpreads(args);
  const char* spreadMsg = "a spread argument must either have a tuple type or be passed to a rest parameter";
  size_t argIdx = 0;
  auto checkArg = [&](Expr*& a, Type* pt) {
    size_t i = argIdx++;
    Type* t = a->type ? a->type : check(a, pt);
    // a void call has no value to pass (a parameter of type T instantiated with void)
    if (t->t == TT::Void) {
      err(a->loc, "a void value cannot be passed as an argument (the function returns nothing in E7)");
      return;
    }
    // f(x) with x: T | undefined for `p: T = d`: JavaScript applies the default when the value is
    // undefined; E7 applies it when the argument is left out, so the call passes x ?? d itself
    if (hasUndefined(t) && !hasUndefined(pt) && i < ft->optParams.size() && ft->optParams[i] &&
        (t->t == TT::Undefined || (t->t == TT::Optional && !t->hasNull && assignable(t->elem, pt)))) {
      Expr* d = fs && fs->params && i < fs->params->size() && (*fs->params)[i].init ? constDefault((*fs->params)[i].init) : nullptr;
      if (!d) {
        err(a->loc, "type '" + typeName(t) + "' is not assignable to type '" + typeName(pt) +
                        "': E7 applies a parameter's default value only when the argument is left out (or when the default "
                        "is a constant); pass 'value ?? <default>'");
        return;
      }
      Type* dt = check(d, pt);
      if (!assignable(dt, pt)) { coerce(a, pt); return; }
      coerce(d, pt);
      if (t->t == TT::Undefined && (stripConvs(a)->k == EK::Undefined)) {  // f(undefined): the default itself
        a = d;
        return;
      }
      Expr* q = P.arena.expr(EK::Logical, a->loc);
      q->raw = "??";
      q->a = a;
      q->b = d;
      q->type = pt;
      a = q;
      return;
    }
    coerce(a, pt);
  };
  size_t np = ft->params.size();
  size_t minArgs = 0;
  for (size_t i = 0; i < np; i++)
    if (!ft->optParams[i] && !(ft->restParam && i + 1 == np)) minArgs = i + 1;
  if (ft->restParam) {
    // f(a, b, ...rest): the remaining arguments (and spread arrays) are packed into a new array
    size_t fixed = np - 1;
    if (args.size() < minArgs) { err(call->loc, "expected at least " + std::to_string(minArgs) + " argument(s), got " + std::to_string(args.size())); return; }
    for (size_t i = 0; i < args.size() && i < fixed; i++) {
      if (args[i]->k == EK::Spread) { err(args[i]->loc, spreadMsg); return; }
      checkArg(args[i], ft->params[i]);
    }
    if (args.size() <= fixed) return;  // the rest parameter is an empty array (see genArgs)
    Type* restT = ft->params.back();
    Expr* arr = P.arena.expr(EK::Array, call->loc);
    for (size_t i = fixed; i < args.size(); i++) arr->list.push_back(args[i]);
    check(arr, restT);
    coerce(arr, restT);
    args.resize(fixed);
    args.push_back(arr);
    call->bval = true;  // rest packed
    return;
  }
  for (Expr* a : args)
    if (a->k == EK::Spread) { err(a->loc, spreadMsg); return; }
  if (args.size() < minArgs || args.size() > np) {
    std::string exp = minArgs == np ? std::to_string(np) : std::to_string(minArgs) + "-" + std::to_string(np);
    err(call->loc, "expected " + exp + " argument(s), got " + std::to_string(args.size()));
    return;
  }
  for (size_t i = 0; i < args.size(); i++) checkArg(args[i], ft->params[i]);
}

// ------------------------------------------------------------------ calls
Type* Checker::checkCall(Expr* e, Type* expected) {
  if (e->tagged) {
    e->tagged = false;
    Expr* tpl = e->list[0];
    Expr* tag = strip(e->a);
    // String.raw`...`: the raw text of the parts with the values in between (a template)
    if (tag->k == EK::Member && tag->raw == "raw" && strip(tag->a)->k == EK::Ident && strip(tag->a)->raw == "String" &&
        (!lookup("String") || lookup("String")->k == SymK::Namespace)) {
      Expr saved = *tpl;
      *e = saved;
      e->quasis = e->rawQuasis;
      return check(e, expected);
    }
    // tag(strings, ...values): the parts as a readonly string[] (TypeScript's TemplateStringsArray)
    Expr* strs = P.arena.expr(EK::Array, tpl->loc);
    for (auto& q : tpl->quasis) {
      Expr* sx = P.arena.expr(EK::Str, tpl->loc);
      sx->str = q;
      strs->list.push_back(sx);
    }
    e->list[0] = strs;
  }
  Expr* c = strip(e->a);
  if (!e->typeArgs.empty() && !(c->k == EK::Member)) {
    // generic call type arguments are only meaningful for a few builtins
  }
  if (c->k == EK::Super) {
    FuncSym* f = curFn;
    while (f && f->isArrowThis && f->parent) f = f->parent;
    if (!f || !f->isCtor || !f->cls->parent) return errT(e->loc, "'super(...)' is only allowed in a derived class constructor");
    FuncSym* pc = f->cls->parent->ctor;
    resolveSig(pc);
    checkArgs(e, pc->type, e->list, 0, pc);
    e->callee = pc;
    e->intrinsic = IN_SUPER_CTOR;
    Sym* th = lookup("this");
    if (th) { e->var = static_cast<VarSym*>(th); markCapture(e->var); }
    return T.voidT();
  }
  if (c->k == EK::Ident) {
    Sym* s = lookup(c->raw);
    if (!s) return errT(c->loc, "cannot find name '" + c->raw + "'" + suggestName(c->raw, false));
    if (s->k == SymK::Generic) {
      GenericSym* g = static_cast<GenericSym*>(s);
      if (g->kind == GenericSym::Class) return errT(e->loc, "class constructor '" + c->raw + "' cannot be invoked without 'new'");
      if (g->kind != GenericSym::Func && g->kind != GenericSym::Arrow) return errT(c->loc, "'" + c->raw + "' is a type, not a value");
      std::vector<Type*> targs;
      if (!inferCall(g, g->fnParams(), g->fnRet(), e->list, e->typeArgs, expected, e->loc, targs)) {
        for (Expr* a : e->list) if (!a->type && a->k != EK::Spread) check(a, nullptr);
        return T.unknown();
      }
      FuncSym* f = instantiateFunc(g, targs, e->loc);
      if (!f) return T.unknown();
      checkArgs(e, f->type, e->list, 0, f);
      if (f->inferRet) {
        if (f->checking) return errT(e->loc, "'" + f->name + "' is called recursively; add an explicit return type");
        checkFunction(f);
      }
      e->callee = f;
      e->intrinsic = IN_DIRECT;
      if (f->selfVar) markCapture(f->selfVar);
      return f->type->ret;
    }
    if (!e->typeArgs.empty() && s->k != SymK::Builtin && s->k != SymK::Namespace)
      return errT(e->loc, "'" + c->raw + "' is not generic: it takes no type arguments");
    if (s->k == SymK::Builtin) return checkBuiltinCall(e, static_cast<BuiltinSym*>(s)->id, expected);
    if (s->k == SymK::Namespace && static_cast<NamespaceSym*>(s)->builtin) {
      // Number(x), String(x), Boolean(x), BigInt(x)
      return checkBuiltinCall(e, static_cast<NamespaceSym*>(s)->module + "()", expected);
    }
    if (s->k == SymK::Func) {
      FuncSym* f = static_cast<FuncSym*>(s);
      resolveSig(f);
      checkArgs(e, f->type, e->list, 0, f);
      if (f->inferRet) {
        if (f->checking) return errT(e->loc, "'" + f->name + "' is called recursively; add an explicit return type");
        checkFunction(f);
      }
      e->callee = f;
      e->intrinsic = IN_DIRECT;
      if (f->selfVar) markCapture(f->selfVar);
      if (f->isNative) e->native = f->name;
      return f->type->ret;
    }
    if (s->k == SymK::Class) return errT(e->loc, "class constructor '" + c->raw + "' cannot be invoked without 'new'");
  }
  if (c->k == EK::Member) return checkMethodCall(e, c, expected);
  Type* ft = check(e->a, nullptr);
  if (ft->t == TT::Unknown) { for (Expr* a : e->list) check(a, nullptr); return T.unknown(); }
  if (ft->t == TT::Optional) {
    if (!e->optional) return errT(e->loc, "cannot call a possibly null/undefined value");
    ft = ft->elem;
  }
  if (ft->t != TT::Func) return errT(e->loc, "type '" + typeName(ft) + "' is not callable");
  checkArgs(e, ft, e->list);
  e->intrinsic = IN_CLOSURE_CALL;
  if (e->optional) {
    e->innerType = ft->ret;
    return ft->ret->t == TT::Void ? T.voidT() : T.optional(ft->ret, false, true);
  }
  return ft->ret;
}

Type* Checker::checkMethodCall(Expr* e, Expr* m, Type* expected) {
  Expr* obj = strip(m->a);
  const std::string& name = m->raw;
  // `x.m?.()`: methods always exist, so the call-level '?.' only matters for optional function fields
  bool callOptional = e->optional;
  e->optional = false;
  if (obj->k == EK::Ident) {
    Sym* s = lookup(obj->raw);
    if (s && s->k == SymK::Namespace) return checkNamespaceMember(e, static_cast<NamespaceSym*>(s), name, true, expected);
    if (ClassSym* c = classOf(s)) {
      resolveClass(c);
      if (name[0] == '#' && !checkPrivateAccess(m->nameLoc, name, c)) return T.unknown();
      // static methods are inherited (looked up in the base classes; they have no `this`, so the call
      // is the same through any subclass); #private ones are not
      ClassSym* cn = c;
      for (ClassSym* k = c; k && name[0] != '#'; k = k->staticParent) {
        auto g = k->genericMethods.find(name);
        if ((g != k->genericMethods.end() && g->second->isStatic) || k->staticMethods.count(name) ||
            k->staticFields.count(name)) {
          c = k;
          break;
        }
      }
      auto gm = c->genericMethods.find(name);
      if (gm != c->genericMethods.end() && gm->second->isStatic) {
        // static generic method
        GenericSym* g = gm->second;
        std::vector<Type*> targs;
        if (!inferCall(g, g->fnParams(), g->fnRet(), e->list, e->typeArgs, expected, e->loc, targs)) return T.unknown();
        FuncSym* f = instantiateFunc(g, targs, e->loc);
        if (!f) return T.unknown();
        checkArgs(e, f->type, e->list, 0, f);
        if (f->inferRet) {
          if (f->checking) return errT(e->loc, "'" + name + "' is called recursively; add an explicit return type");
          checkFunction(f);
        }
        e->callee = f;
        e->intrinsic = IN_DIRECT;
        return f->type->ret;
      }
      auto it = c->staticMethods.find(name);
      if (it == c->staticMethods.end()) {
        auto f = c->staticFields.find(name);
        if (f != c->staticFields.end()) {
          Type* ft = check(m, nullptr);
          if (ft->t != TT::Func) return errT(e->loc, "'" + name + "' is not callable");
          checkArgs(e, ft, e->list);
          e->intrinsic = IN_CLOSURE_CALL;
          return ft->ret;
        }
        return errT(e->loc, "class '" + cn->name + "' has no static method '" + name + "'");
      }
      FuncSym* f = it->second;
      resolveSig(f);
      checkArgs(e, f->type, e->list, 0, f);
      if (f->inferRet) {
        if (f->checking) return errT(e->loc, "'" + name + "' is called recursively; add an explicit return type");
        checkFunction(f);
      }
      e->callee = f;
      e->intrinsic = IN_DIRECT;
      return f->type->ret;
    }
  }
  if (obj->k == EK::Super) {
    FuncSym* f = curFn;
    while (f && f->isArrowThis && f->parent && !f->isMethod) f = f->parent;
    if (!f || !f->cls || !f->cls->parent || f->isStatic) return errT(e->loc, "'super' method calls are only allowed in methods of derived classes");
    ClassSym* pc = f->cls->parent;
    auto it = pc->methodIdx.find(name);
    if (it == pc->methodIdx.end()) return errT(e->loc, "base class has no method '" + name + "'");
    FuncSym* target = pc->vtable[it->second].fn;
    resolveSig(target);
    if (target->inferRet && !target->checking) checkFunction(target);
    checkArgs(e, target->type, e->list, 0, target);
    e->callee = target;
    e->intrinsic = IN_SUPER_METHOD;
    Sym* th = lookup("this");
    if (th) { e->var = static_cast<VarSym*>(th); markCapture(e->var); }
    return target->type->ret;
  }
  Type* ot = check(m->a, nullptr);
  if (ot->t == TT::Unknown) { for (Expr* a : e->list) check(a, nullptr); return T.unknown(); }
  Type* bt = ot;
  if (ot->t == TT::Optional) {
    if (!m->optional || (m->chained && m->a && (m->a->optional ? m->a->innerType && m->a->innerType->t == TT::Optional : m->a->type && m->a->type->t == TT::Optional)))
      return errT(m->loc, "object is possibly null or undefined; use '?.' or a check");
    bt = ot->elem;
    e->optional = true;
  }
  Type* ret = nullptr;
  switch (bt->t) {
    case TT::Class: {
      ClassSym* c = bt->cls;
      resolveClass(c);
      GenericSym* gmeth = nullptr;
      for (ClassSym* k = c; k && !gmeth; k = k->parent) {
        auto gi = k->genericMethods.find(name);
        if (gi != k->genericMethods.end() && !gi->second->isStatic) gmeth = gi->second;
      }
      if (gmeth) {
        // generic method: instantiated per type argument list, called directly with this
        std::vector<Type*> targs;
        if (!inferCall(gmeth, gmeth->fnParams(), gmeth->fnRet(), e->list, e->typeArgs, expected, e->loc, targs)) return T.unknown();
        FuncSym* f = instantiateFunc(gmeth, targs, e->loc);
        if (!f) return T.unknown();
        checkArgs(e, f->type, e->list, 0, f);
        if (f->inferRet) {
          if (f->checking) return errT(e->loc, "method '" + name + "' is called recursively; add an explicit return type");
          checkFunction(f);
        }
        e->callee = f;
        e->intrinsic = IN_PRIVATE_METHOD;
        ret = f->type->ret;
        break;
      }
      if (name[0] == '#') {
        // #private method: not virtual, called directly
        FuncSym* pf = nullptr;
        ClassSym* owner = nullptr;
        for (ClassSym* k = c; k && !pf; k = k->parent) {
          auto pi = k->privateMethods.find(name);
          if (pi != k->privateMethods.end()) { pf = pi->second; owner = k; }
        }
        if (pf) {
          if (!checkPrivateAccess(m->nameLoc, name, owner)) return T.unknown();
          resolveSig(pf);
          if (pf->inferRet) {
            if (pf->checking) return errT(e->loc, "method '" + name + "' is called recursively; add an explicit return type");
            checkFunction(pf);
          }
          checkArgs(e, pf->type, e->list, 0, pf);
          e->callee = pf;
          e->intrinsic = IN_PRIVATE_METHOD;
          ret = pf->type->ret;
          break;
        }
        auto fi = c->fieldIdx.find(name);
        if (fi != c->fieldIdx.end() && !checkPrivateAccess(m->nameLoc, name, c->fields[fi->second].owner)) return T.unknown();
      }
      auto it = c->methodIdx.find(name);
      if (it != c->methodIdx.end()) {
        FuncSym* f = c->vtable[it->second].fn;
        resolveSig(f);
        if (f->inferRet) {
          if (f->checking) return errT(e->loc, "method '" + name + "' is called recursively; add an explicit return type");
          checkFunction(f);
        }
        checkArgs(e, f->type, e->list);
        e->callee = f;
        e->vslot = it->second;
        e->cls = c;
        e->intrinsic = IN_METHOD;
        ret = f->type->ret;
        if (f->retThis && ret->t == TT::Class) ret = c->type;  // `this` type: the receiver's class
        break;
      }
      auto fit = c->fieldIdx.find(name);
      if (fit != c->fieldIdx.end()) {
        Type* ft = c->fields[fit->second].type;
        if (callOptional && ft->t == TT::Optional && ft->elem->t == TT::Func && !m->optional) {
          // this.callback?.(...)
          check(m, nullptr);
          m->type = ft;  // keep the nullable storage type; the call checks for null at run time
          Type* fnT = ft->elem;
          checkArgs(e, fnT, e->list);
          e->intrinsic = IN_CLOSURE_CALL;
          e->optional = true;
          e->innerType = fnT->ret;
          return fnT->ret->t == TT::Void ? T.voidT() : T.optional(fnT->ret, false, true);
        }
        if (ft->t == TT::Optional && ft->elem->t == TT::Func && !m->optional) {
          // an optional function field narrowed to non-null (`if (this.cb) this.cb(x)`): a closure call
          Type* nt = check(m, nullptr);
          if (nt->t != TT::Func)
            return errT(m->loc, "cannot call '" + name + "': it is possibly undefined (use '" + name + "?.(...)' or check it first)");
          checkArgs(e, nt, e->list);
          e->intrinsic = IN_CLOSURE_CALL;
          ret = nt->ret;
          break;
        }
        if (ft->t != TT::Func) return errT(e->loc, "property '" + name + "' is not callable");
        m->field = fit->second;
        m->cls = c;
        m->type = ft;
        checkArgs(e, ft, e->list);
        e->intrinsic = IN_FIELD_CALL;
        ret = ft->ret;
        break;
      }
      return errT(m->loc, "property '" + name + "' does not exist on type '" + c->name + "'" + suggestMember(bt, name));
    }
    case TT::Object: {
      const ObjShape::Method* mm = bt->shape->findMethod(name);
      if (mm) {
        checkArgs(e, mm->fn, e->list);
        e->intrinsic = IN_IFACE_METHOD;
        ret = mm->fn->ret;
        m->type = mm->fn;  // signature for codegen
        break;
      }
      const ObjShape::Prop* p = bt->shape->findProp(name);
      if (p && callOptional && p->type->t == TT::Optional && p->type->elem->t == TT::Func && !m->optional) {
        check(m, nullptr);
        m->type = p->type;
        Type* fnT = p->type->elem;
        checkArgs(e, fnT, e->list);
        e->intrinsic = IN_CLOSURE_CALL;
        e->optional = true;
        e->innerType = fnT->ret;
        return fnT->ret->t == TT::Void ? T.voidT() : T.optional(fnT->ret, false, true);
      }
      if (p && p->type->t == TT::Func) {
        m->type = p->type;
        checkArgs(e, p->type, e->list);
        e->intrinsic = IN_FIELD_CALL;
        ret = p->type->ret;
        break;
      }
      if (p && p->type->t == TT::Optional && p->type->elem->t == TT::Func && !m->optional) {
        // an optional function property narrowed to non-null (`if (o.f) o.f(x)`): a closure call
        Type* nt = check(m, nullptr);
        if (nt->t == TT::Func) {
          checkArgs(e, nt, e->list);
          e->intrinsic = IN_CLOSURE_CALL;
          ret = nt->ret;
          break;
        }
        return errT(m->loc, "cannot call '" + name + "': it is possibly undefined (use '" + name + "?.(...)' or check it first)");
      }
      return errT(m->loc, "property '" + name + "' does not exist or is not callable on type '" + typeName(bt) + "'" +
                              suggestMember(bt, name));
    }
    default:
      ret = checkBuiltinMethod(e, m, bt, expected);
      if (!ret) return T.unknown();
      break;
  }
  if (e->optional) {
    e->innerType = ret;
    return ret->t == TT::Void ? T.voidT() : T.optional(ret, false, true);
  }
  return ret;
}

// Callback helper: checks `arg` as a function compatible with params -> ret (ret may be null = infer)
static Type* callbackType(Checker* ck, Program& P, Types& T, Expr* arg, const std::vector<Type*>& params, Type* ret,
                          std::function<Type*(Expr*, Type*)> chk, std::function<void(Loc, const std::string&)> error) {
  (void)P;
  Type* exp = T.func(params, ret ? ret : T.unknown());
  Type* t = chk(arg, exp);
  if (t->t == TT::Unknown) return t;
  if (t->t != TT::Func) { error(arg->loc, "expected a function"); return T.unknown(); }
  if (t->params.size() > params.size()) { error(arg->loc, "callback has too many parameters"); return T.unknown(); }
  for (size_t i = 0; i < t->params.size(); i++)
    if (t->params[i] != params[i] && t->params[i]->t != TT::Unknown) {
      error(arg->loc, "callback parameter " + std::to_string(i + 1) + " has type '" + typeName(t->params[i]) + "', expected '" + typeName(params[i]) + "'");
      return T.unknown();
    }
  if (t->restParam) { error(arg->loc, "rest parameters are not allowed in callbacks"); return T.unknown(); }
  (void)ck;
  return t;
}

// ------------------------------------------------------------------ builtin methods / properties
Type* Checker::checkBuiltinMethod(Expr* e, Expr* m, Type* bt, Type* expected) {
  const std::string& name = m->raw;
  bool isCall = e != m;
  std::vector<Expr*> noArgs;
  std::vector<Expr*>& args = isCall ? e->list : noArgs;
  Loc l = e->loc;
  auto prop = [&](const char* nm) { return !isCall && name == nm; };
  auto meth = [&](const char* nm) { return isCall && name == nm; };
  auto native = [&](const std::string& n) {
    e->native = n;
    e->intrinsic = IN_NATIVE;
  };
  auto N = [&](double v) { return mkNum(P, T, v, l); };
  auto S = [&](const std::u16string& v) { return mkStr(P, T, v, l); };
  auto noSuch = [&]() -> Type* {
    return errT(m->loc, std::string(isCall ? "method" : "property") + " '" + name + "' does not exist on type '" + typeName(bt) + "'" +
                            suggestMember(bt, name));
  };
  // readonly T[] / ReadonlyMap / ReadonlySet have no mutating methods (as in TypeScript)
  if (bt->ro && isCall) {
    static const char* const arrMut[] = {"push", "pop", "shift", "unshift", "splice", "sort", "reverse", "fill", "copyWithin"};
    static const char* const mapMut[] = {"set", "delete", "clear"};
    static const char* const setMut[] = {"add", "delete", "clear"};
    bool mut = false;
    if (bt->t == TT::Array) for (const char* x : arrMut) mut = mut || name == x;
    if (bt->t == TT::Map) for (const char* x : mapMut) mut = mut || name == x;
    if (bt->t == TT::Set) for (const char* x : setMut) mut = mut || name == x;
    if (mut) return errT(m->loc, "property '" + name + "' does not exist on type '" + typeName(bt) + "'");
  }
  auto cb = [&](Expr* arg, const std::vector<Type*>& ps, Type* ret) {
    return callbackType(this, P, T, arg, ps, ret, [&](Expr* x, Type* t) { return check(x, t); },
                        [&](Loc lo, const std::string& msg) { err(lo, msg); });
  };
  Type* num = T.number();
  Type* str = T.string();
  Type* boo = T.boolean();
  switch (bt->t) {
    case TT::String: {
      if (prop("length")) { e->intrinsic = IN_ARR_LEN; return num; }
      if (meth("toString") || meth("valueOf")) {
        if (!argsMatch(e, args, {}, 0, "toString")) return str;
        e->intrinsic = IN_TOSTRING;
        return str;
      }
      if (meth("charAt") || meth("charCodeAt") || meth("codePointAt") || meth("at")) {
        if (args.empty() && name != "at") args.push_back(N(0));
        if (!argsMatch(e, args, {num}, 1, name.c_str())) return T.unknown();
        native("String." + name);
        if (name == "charAt") return str;
        if (name == "charCodeAt") return num;
        if (name == "codePointAt") return T.optional(num, false, true);
        return T.optional(str, false, true);
      }
      if (meth("indexOf") || meth("lastIndexOf") || meth("includes") || meth("startsWith") || meth("endsWith")) {
        double def = (name == "lastIndexOf" || name == "endsWith") ? INFINITY : 0;
        if (args.size() == 1) args.push_back(N(def));
        if (!argsMatch(e, args, {str, num}, 2, name.c_str())) return T.unknown();
        native("String." + name);
        return (name == "indexOf" || name == "lastIndexOf") ? num : boo;
      }
      if (meth("slice") || meth("substring") || meth("substr")) {
        if (args.empty()) args.push_back(N(0));
        if (args.size() == 1) args.push_back(N(INFINITY));
        if (!argsMatch(e, args, {num, num}, 2, name.c_str())) return T.unknown();
        native("String." + name);
        return str;
      }
      if (meth("toUpperCase") || meth("toLowerCase") || meth("trim") || meth("trimStart") || meth("trimEnd") ||
          meth("toLocaleUpperCase") || meth("toLocaleLowerCase") || meth("trimLeft") || meth("trimRight")) {
        if (!argsMatch(e, args, {}, 0, name.c_str())) return str;
        std::string n = name;
        if (n == "toLocaleUpperCase") n = "toUpperCase";
        if (n == "toLocaleLowerCase") n = "toLowerCase";
        if (n == "trimLeft") n = "trimStart";
        if (n == "trimRight") n = "trimEnd";
        native("String." + n);
        return str;
      }
      if (meth("padStart") || meth("padEnd")) {
        if (args.size() == 1) args.push_back(S(u" "));
        if (!argsMatch(e, args, {num, str}, 2, name.c_str())) return T.unknown();
        native("String." + name);
        return str;
      }
      if (meth("repeat")) {
        if (!argsMatch(e, args, {num}, 1, "repeat")) return T.unknown();
        native("String.repeat");
        return str;
      }
      if (meth("split")) {
        if (args.empty()) return errT(e->loc, "split() without a separator is not supported");
        if (args.size() == 1) args.push_back(N(4294967295.0));
        if (!argsMatch(e, args, {str, num}, 2, "split")) return T.unknown();
        native("String.split");
        return T.array(str);
      }
      if ((meth("replace") || meth("replaceAll")) && args.size() == 2 && args[1]->k != EK::Spread &&
          (strip(args[1])->k == EK::Arrow || check(args[1], nullptr)->t == TT::Func)) {
        // s.replace(pattern, (match, offset, string) => replacement): compiled to a loop (indexOf / slice)
        if (args[0]->k == EK::Spread) return errT(args[0]->loc, "spread is not supported here");
        check(args[0], str);
        if (!coerce(args[0], str)) return T.unknown();
        Type* ft = cb(args[1], {str, num, str}, str);
        if (ft->t != TT::Func) return T.unknown();
        if (ft->ret != str) return errT(args[1]->loc, "the replacement function must return a string");
        e->intrinsic = IN_STR_REPLACE_FN;
        e->bval = name == "replaceAll";
        return str;
      }
      if (meth("replace") || meth("replaceAll")) {
        if (!argsMatch(e, args, {str, str}, 2, name.c_str())) return T.unknown();
        native("String." + name);
        return str;
      }
      if (meth("localeCompare")) {
        if (!argsMatch(e, args, {str}, 1, "localeCompare")) return T.unknown();
        native("String.localeCompare");
        return num;
      }
      if (meth("concat")) {
        for (Expr* a : args) {
          Type* t = check(a, nullptr);
          if (t->t == TT::Void) err(a->loc, "cannot concatenate void");
        }
        e->intrinsic = IN_STR_CONCAT;
        return str;
      }
      if (meth("match") || meth("matchAll") || meth("search")) return errT(m->loc, kNoRegExp);
      if (meth("normalize")) return errT(m->loc, "'normalize' is not supported (no Unicode normalization data)");
      return noSuch();
    }
    case TT::Number:
    case TT::Enum: {
      if (bt->t == TT::Enum && bt->en->isString) {
        if (meth("toString")) { e->intrinsic = IN_TOSTRING; return str; }
        // a string enum's values are strings: the string methods, on the receiver seen as a string
        m->a = wrapConv(m->a, ConvKind::None, str);
        return checkBuiltinMethod(e, m, str, expected);
      }
      if (meth("toString")) {
        if (args.empty()) { e->intrinsic = IN_TOSTRING; return str; }
        if (!argsMatch(e, args, {num}, 1, "toString")) return T.unknown();
        native("Number.toStringRadix");
        return str;
      }
      if (meth("toFixed") || meth("toExponential") || meth("toPrecision")) {
        if (args.empty()) {
          if (name == "toPrecision") { e->intrinsic = IN_TOSTRING; return str; }
          args.push_back(N(name == "toFixed" ? 0 : NAN));
        }
        if (!argsMatch(e, args, {num}, 1, name.c_str())) return T.unknown();
        native("Number." + name);
        return str;
      }
      if (meth("valueOf")) { e->intrinsic = IN_NUMBER; return num; }
      return noSuch();
    }
    case TT::Int: {
      if (meth("toString")) {
        if (args.empty()) { e->intrinsic = IN_TOSTRING; return str; }
        if (!argsMatch(e, args, {num}, 1, "toString")) return T.unknown();
        args.push_back(N((double)bt->vt));
        native("Int.toStringRadix");
        return str;
      }
      return noSuch();
    }
    case TT::Bool: {
      if (meth("toString")) { e->intrinsic = IN_TOSTRING; return str; }
      return noSuch();
    }
    case TT::BigInt: {
      if (meth("toString")) {
        if (args.empty()) args.push_back(N(10));
        if (!argsMatch(e, args, {num}, 1, "toString")) return T.unknown();
        native("BigInt.toString");
        return str;
      }
      return noSuch();
    }
    case TT::Array: {
      Type* et = bt->elem;
      std::string K = kindSuffix(et);
      if (prop("length")) { e->intrinsic = IN_ARR_LEN; return num; }
      if (meth("push")) {
        if (args.size() == 1 && args[0]->k == EK::Spread) {
          Type* st = check(args[0]->a, bt);
          if (st != bt) return errT(args[0]->loc, "spread argument must be of type '" + typeName(bt) + "'");
          args[0] = args[0]->a;
          native("Array.pushArray");
          return num;
        }
        if (args.empty()) return errT(e->loc, "push requires at least one argument");
        for (Expr*& a : args) {
          if (a->k == EK::Spread) return errT(a->loc, "spread must be the only argument of push");
          check(a, et);
          coerce(a, et);
        }
        e->intrinsic = IN_ARR_PUSH;
        return num;
      }
      if (meth("pop") || meth("shift")) {
        if (!argsMatch(e, args, {}, 0, name.c_str())) return T.unknown();
        native("Array." + name);
        return T.optional(et, false, true);
      }
      if (meth("unshift")) {
        if (!argsMatch(e, args, {et}, 1, "unshift")) return T.unknown();
        native("Array.unshift" + K);
        return num;
      }
      if (meth("at")) {
        if (!argsMatch(e, args, {num}, 1, "at")) return T.unknown();
        native("Array.at");
        return T.optional(et, false, true);
      }
      if (meth("slice")) {
        if (args.empty()) args.push_back(N(0));
        if (args.size() == 1) args.push_back(N(INFINITY));
        if (!argsMatch(e, args, {num, num}, 2, "slice")) return T.unknown();
        native("Array.slice");
        return T.array(bt->elem);  // a new (mutable) array, also from a readonly one
      }
      // ES2023 copying methods (also on readonly arrays: they return a new mutable array)
      if (meth("toReversed")) {
        if (!argsMatch(e, args, {}, 0, "toReversed")) return T.unknown();
        native("Array.toReversed");
        return T.array(bt->elem);
      }
      if (meth("toSorted")) {
        if (args.empty()) { native("Array.toSorted"); return T.array(bt->elem); }
        if (args.size() != 1) return errT(e->loc, "toSorted takes at most one argument");
        Type* ft = cb(args[0], {et, et}, num);
        if (ft->t == TT::Func && ft->params.size() != 2) return errT(args[0]->loc, "toSorted comparator must take two parameters");
        if (ft->t == TT::Func && ft->ret != num) return errT(args[0]->loc, "toSorted comparator must return a number");
        native("Array.toSortedCmp");
        return T.array(bt->elem);
      }
      if (meth("with")) {
        if (!argsMatch(e, args, {num, et}, 2, "with")) return T.unknown();
        native("Array.with" + K);
        return T.array(bt->elem);
      }
      if (meth("toSpliced")) {
        // (a start is required, as in TypeScript's declaration; JavaScript's toSpliced() copies the array)
        if (args.empty()) return errT(l, "toSpliced expects a start index (use slice() for a copy)");
        if (args.size() == 1) args.push_back(N(INFINITY));
        Expr* items;
        if (args.size() > 2) {
          items = P.arena.expr(EK::Array, l);
          for (size_t i = 2; i < args.size(); i++) items->list.push_back(args[i]);
          args.resize(2);
          check(items, T.array(bt->elem));
        } else {
          items = P.arena.expr(EK::Null, l);
          items->type = T.nullT();
        }
        if (!argsMatch(e, args, {num, num}, 2, "toSpliced")) return T.unknown();
        args.push_back(items);
        native("Array.toSpliced");
        return T.array(bt->elem);
      }
      if (meth("splice")) {
        if (args.empty()) return errT(e->loc, "splice requires a start index");
        if (args.size() == 1) args.push_back(N(INFINITY));
        if (args.size() > 2) {
          Expr* items = P.arena.expr(EK::Array, l);
          for (size_t i = 2; i < args.size(); i++) items->list.push_back(args[i]);
          args.resize(2);
          check(items, bt);
          args.push_back(items);
        } else {
          Expr* items = P.arena.expr(EK::Null, l);
          items->type = T.nullT();
          args.push_back(items);
        }
        std::vector<Type*> ps = {num, num, T.optional(bt, true, false)};
        Expr* last = args[2];
        args.pop_back();
        if (!argsMatch(e, args, {num, num}, 2, "splice")) return T.unknown();
        args.push_back(last);
        native("Array.splice");
        return bt;
      }
      if (meth("concat")) {
        if (args.empty()) { native("Array.slice"); args.push_back(N(0)); args.push_back(N(INFINITY)); check(args[0], num); check(args[1], num); return T.array(bt->elem); }
        for (Expr*& a : args) {
          Type* t = check(a, T.array(bt->elem));
          if (t != T.array(bt->elem) && t != T.array(bt->elem, true)) return errT(a->loc, "concat arguments must be arrays of type '" + typeName(bt) + "'");
        }
        native("Array.concat");
        return T.array(bt->elem);
      }
      if (meth("indexOf") || meth("lastIndexOf")) {
        if (args.size() == 1) args.push_back(N(name == "indexOf" ? 0 : INFINITY));
        if (!argsMatch(e, args, {et, num}, 2, name.c_str())) return T.unknown();
        native("Array." + name + K);
        return num;
      }
      if (meth("includes")) {
        if (!argsMatch(e, args, {et}, 1, "includes")) return T.unknown();
        native("Array.includes" + K);
        return boo;
      }
      if (meth("join") || meth("toString")) {
        if (name == "toString" && !args.empty()) return errT(e->loc, "toString takes no arguments");
        if (args.empty()) args.push_back(S(u","));
        if (!argsMatch(e, args, {str}, 1, "join")) return T.unknown();
        native("Array.join");
        return str;
      }
      if (meth("reverse")) {
        if (!argsMatch(e, args, {}, 0, "reverse")) return T.unknown();
        native("Array.reverse");
        return bt;
      }
      if (meth("fill")) {
        if (args.size() == 1) args.push_back(N(0));
        if (args.size() == 2) args.push_back(N(INFINITY));
        if (!argsMatch(e, args, {et, num, num}, 3, "fill")) return T.unknown();
        native("Array.fill" + K);
        return bt;
      }
      if (meth("sort")) {
        if (args.empty()) { native("Array.sort"); return bt; }
        if (args.size() != 1) return errT(e->loc, "sort takes at most one argument");
        Type* ft = cb(args[0], {et, et}, num);
        if (ft->t == TT::Func && ft->params.size() != 2) return errT(args[0]->loc, "sort comparator must take two parameters");
        if (ft->t == TT::Func && ft->ret != num) return errT(args[0]->loc, "sort comparator must return a number");
        native("Array.sortCmp");
        return bt;
      }
      if (meth("map") || meth("filter") || meth("forEach") || meth("some") || meth("every") || meth("find") ||
          meth("findIndex") || meth("findLast") || meth("findLastIndex")) {
        if (args.size() != 1) return errT(e->loc, "'" + name + "' expects one callback argument");
        Type* want = nullptr;
        if (name == "filter" || name == "some" || name == "every" || name == "find" || name == "findIndex" ||
            name == "findLast" || name == "findLastIndex")
          want = boo;
        if (name == "map" && expected && baseOf(expected)->t == TT::Array) want = baseOf(expected)->elem;
        Type* ft = cb(args[0], {et, num, bt}, want);
        if (ft->t != TT::Func) return T.unknown();
        Type* r = ft->ret;
        if (want == boo && r != boo) return errT(args[0]->loc, "callback of '" + name + "' must return a boolean");
        if (name == "map") {
          if (r->t == TT::Void) return errT(args[0]->loc, "map callback must return a value");
          e->intrinsic = IN_ARR_MAP;
          return T.array(r);
        }
        // a type predicate callback ((x): x is string => x !== null): the elements found are of its type
        // (when they are stored the same way: references)
        Type* pt = nullptr;
        if (name == "filter" || name == "find" || name == "findLast") {
          Expr* c = strip(args[0]);
          while (c->k == EK::Conv) c = strip(c->a);
          FuncSym* pf = c->k == EK::Arrow ? c->fn : nullptr;
          if (!pf && c->k == EK::Ident) {
            Sym* sy = lookup(c->raw);
            if (sy && sy->k == SymK::Func) pf = static_cast<FuncSym*>(sy);
            else if (sy && sy->k == SymK::Var) {
              Expr call;
              call.k = EK::Call;
              call.a = c;
              pf = predCallee(&call);
            }
          }
          if (pf && pf->predParam == 0 && pf->predType && !pf->asserts && kindOf(pf->predType) == kindOf(et) &&
              kindOf(et) == K_R && assignable(pf->predType, et))
            pt = pf->predType;
        }
        if (name == "filter") { e->intrinsic = IN_ARR_FILTER; return T.array(pt ? pt : bt->elem); }
        if (name == "forEach") { e->intrinsic = IN_ARR_FOREACH; return T.voidT(); }
        if (name == "some") { e->intrinsic = IN_ARR_SOME; return boo; }
        if (name == "every") { e->intrinsic = IN_ARR_EVERY; return boo; }
        if (name == "find") { e->intrinsic = IN_ARR_FIND; return T.optional(pt ? pt : et, false, true); }
        if (name == "findLast") { e->intrinsic = IN_ARR_FINDLAST; return T.optional(pt ? pt : et, false, true); }
        if (name == "findIndex") { e->intrinsic = IN_ARR_FINDINDEX; return num; }
        e->intrinsic = IN_ARR_FINDLASTINDEX;
        return num;
      }
      if (meth("reduce") || meth("reduceRight")) {
        if (args.empty() || args.size() > 2) return errT(e->loc, "'" + name + "' expects a callback and an optional initial value");
        Type* acc = et;
        if (args.size() == 2) {
          Type* exp = expected ? baseOf(expected) : nullptr;
          acc = check(args[1], exp);
          if (exp && acc != exp && assignable(acc, exp)) { coerce(args[1], exp); acc = exp; }
          if (acc->t == TT::Null || acc->t == TT::Undefined) return errT(args[1]->loc, "cannot infer accumulator type from null; add a type annotation");
        }
        Type* ft = cb(args[0], {acc, et, num, bt}, acc);
        if (ft->t != TT::Func) return T.unknown();
        if (ft->ret != acc) return errT(args[0]->loc, "reduce callback must return '" + typeName(acc) + "'");
        e->intrinsic = name == "reduce" ? IN_ARR_REDUCE : IN_ARR_REDUCE_RIGHT;
        return acc;
      }
      if (meth("keys") || meth("values") || meth("entries"))
        return errT(m->loc, "'" + name + "()' on arrays is only supported directly in for...of");
      if (meth("flatMap")) {
        // the callback returns an array (flattened one level) or a single value
        if (args.size() != 1) return errT(e->loc, "'flatMap' expects one callback argument");
        Type* ft = cb(args[0], {et, num, bt}, nullptr);
        if (ft->t != TT::Func) return T.unknown();
        Type* r = ft->ret;
        if (r->t == TT::Void) return errT(args[0]->loc, "flatMap callback must return a value");
        e->intrinsic = IN_ARR_FLATMAP;
        e->bval = r->t == TT::Array;
        return r->t == TT::Array ? r : T.array(r);
      }
      if (meth("flat")) {
        // flat(depth): the depth must be a constant (it determines the result type)
        int depth = 1;
        if (args.size() > 1) return errT(e->loc, "'flat' expects at most one argument (the depth)");
        if (args.size() == 1) {
          Expr* d = strip(args[0]);
          bool neg = false;
          if (d->k == EK::Unary && d->raw == "-") { neg = true; d = strip(d->a); }
          if (d->k == EK::Ident && d->raw == "Infinity" && !neg) depth = 1 << 20;
          else if (d->k == EK::Num && d->isIntLit) depth = neg ? 0 : (int)std::min(d->num, 1048576.0);
          else return errT(args[0]->loc, "flat(depth) needs a constant depth (an integer literal or Infinity): it determines the result type");
          check(args[0], num);
        }
        Type* rt = bt;
        int levels = 0;
        while (levels < depth && rt->elem->t == TT::Array) { rt = rt->elem; levels++; }
        e->intrinsic = IN_ARR_FLAT;
        e->ival = levels;
        return rt;
      }
      if (meth("copyWithin")) return errT(m->loc, "'copyWithin' is not supported");
      return noSuch();
    }
    case TT::Bytes: {
      if (prop("length") || prop("byteLength")) { e->intrinsic = IN_ARR_LEN; return num; }
      if (prop("byteOffset")) { native("Bytes.byteOffset"); return num; }
      if (prop("buffer")) { native("Bytes.buffer"); return T.arrayBuffer(); }
      if (meth("set")) {
        if (args.size() == 1) args.push_back(N(0));
        if (args.size() != 2) return errT(e->loc, "set expects (source, offset?)");
        Type* st = check(args[0], nullptr);
        if (st->t != TT::Bytes && !(st->t == TT::Array && st->elem == num)) return errT(args[0]->loc, "set source must be a Uint8Array or number[]");
        if (!argsMatch(e, args, {st, num}, 2, "set")) return T.unknown();
        native("Bytes.set");
        return T.voidT();
      }
      if (meth("subarray") || meth("slice")) {
        if (args.empty()) args.push_back(N(0));
        if (args.size() == 1) args.push_back(N(INFINITY));
        if (!argsMatch(e, args, {num, num}, 2, name.c_str())) return T.unknown();
        native("Bytes." + name);
        return T.bytes();
      }
      if (meth("fill")) {
        if (args.size() == 1) args.push_back(N(0));
        if (args.size() == 2) args.push_back(N(INFINITY));
        if (!argsMatch(e, args, {num, num, num}, 3, "fill")) return T.unknown();
        native("Bytes.fill");
        return T.bytes();
      }
      if (meth("indexOf") || meth("lastIndexOf") || meth("includes")) {
        if (args.size() == 1) args.push_back(N(name == "lastIndexOf" ? INFINITY : 0));
        if (!argsMatch(e, args, {num, num}, 2, name.c_str())) return T.unknown();
        native("Bytes." + name);
        return name == "includes" ? boo : num;
      }
      if (meth("join") || meth("toString")) {
        if (args.empty()) args.push_back(S(u","));
        if (!argsMatch(e, args, {str}, 1, "join")) return T.unknown();
        native("Bytes.join");
        return str;
      }
      if (meth("reverse")) {
        if (!argsMatch(e, args, {}, 0, "reverse")) return T.unknown();
        native("Bytes.reverse");
        return T.bytes();
      }
      if (meth("at")) {
        if (!argsMatch(e, args, {num}, 1, "at")) return T.unknown();
        native("Bytes.at");
        return T.optional(num, false, true);
      }
      if (meth("forEach")) {
        if (args.size() != 1) return errT(e->loc, "forEach expects one callback");
        Type* ft = cb(args[0], {num, num, T.bytes()}, nullptr);
        if (ft->t != TT::Func) return T.unknown();
        e->intrinsic = IN_BYTES_FOREACH;
        return T.voidT();
      }
      return noSuch();
    }
    case TT::ArrayBuffer: {
      if (prop("byteLength")) { native("Buffer.byteLength"); return num; }
      if (meth("slice")) {
        if (args.empty()) args.push_back(N(0));
        if (args.size() == 1) args.push_back(N(INFINITY));
        if (!argsMatch(e, args, {num, num}, 2, "slice")) return T.unknown();
        native("Buffer.slice");
        return T.arrayBuffer();
      }
      return noSuch();
    }
    case TT::DataView: {
      if (prop("byteLength")) { e->intrinsic = IN_ARR_LEN; return num; }
      if (prop("byteOffset")) { native("Bytes.byteOffset"); return num; }
      if (prop("buffer")) { native("Bytes.buffer"); return T.arrayBuffer(); }
      static const char* kinds[] = {"Uint8", "Int8", "Uint16", "Int16", "Uint32", "Int32", "Float32", "Float64"};
      for (int k = 0; k < 8; k++) {
        std::string g = std::string("get") + kinds[k], s = std::string("set") + kinds[k];
        if (meth(g.c_str())) {
          if (args.size() == 1) args.push_back(mkBool(P, T, false, l));
          if (!argsMatch(e, args, {num, boo}, 2, g.c_str())) return T.unknown();
          args.insert(args.begin() + 1, N(k));
          native("DataView.get");
          return num;
        }
        if (meth(s.c_str())) {
          if (args.size() == 2) args.push_back(mkBool(P, T, false, l));
          if (!argsMatch(e, args, {num, num, boo}, 3, s.c_str())) return T.unknown();
          args.insert(args.begin() + 2, N(k));
          native("DataView.set");
          return T.voidT();
        }
      }
      if (meth("getBigUint64") || meth("getBigInt64")) {
        if (args.size() == 1) args.push_back(mkBool(P, T, false, l));
        if (!argsMatch(e, args, {num, boo}, 2, name.c_str())) return T.unknown();
        args.insert(args.begin() + 1, mkBool(P, T, name == "getBigInt64", l));
        native("DataView.getBig");
        return T.bigint();
      }
      if (meth("setBigUint64") || meth("setBigInt64")) {
        if (args.size() == 2) args.push_back(mkBool(P, T, false, l));
        if (!argsMatch(e, args, {num, T.bigint(), boo}, 3, name.c_str())) return T.unknown();
        args.insert(args.begin() + 2, mkBool(P, T, name == "setBigInt64", l));
        native("DataView.setBig");
        return T.voidT();
      }
      return noSuch();
    }
    case TT::TypedArray: {
      Type* et = taElem(bt);
      bool big = et->t == TT::BigInt;
      if (prop("length")) { native("TA.length"); return num; }
      if (prop("byteLength")) { e->intrinsic = IN_ARR_LEN; return num; }
      if (prop("byteOffset")) { native("Bytes.byteOffset"); return num; }
      if (prop("buffer")) { native("TA.buffer"); return T.arrayBuffer(); }
      if (prop("BYTES_PER_ELEMENT")) {
        static const int sz[11] = {1, 1, 1, 2, 2, 4, 4, 4, 8, 8, 8};
        e->intrinsic = IN_CONST_NUM;
        e->num = sz[bt->vt];
        return num;
      }
      if (meth("subarray") || meth("slice")) {
        if (args.empty()) args.push_back(N(0));
        if (args.size() == 1) args.push_back(N(INFINITY));
        if (!argsMatch(e, args, {num, num}, 2, name.c_str())) return T.unknown();
        native(name == "slice" ? "TA.slice" : "TA.subarray");
        return bt;
      }
      if (meth("fill")) {
        if (args.size() == 1) args.push_back(N(0));
        if (args.size() == 2) args.push_back(N(INFINITY));
        if (!argsMatch(e, args, {et, num, num}, 3, "fill")) return T.unknown();
        native(big ? "TA.fillBig" : "TA.fill");
        return bt;
      }
      if (meth("set")) {
        if (args.size() == 1) args.push_back(N(0));
        if (args.size() != 2) return errT(l, "set takes a source and an offset");
        Type* st = check(args[0], T.array(et));
        if (!(st->t == TT::TypedArray || st->t == TT::Bytes || (st->t == TT::Array && baseLit(st->elem) == et)))
          return errT(args[0]->loc, "set needs an array of " + typeName(et) + "s or a typed array");
        Expr* src = args[0];
        args.erase(args.begin());
        if (!argsMatch(e, args, {num}, 1, "set")) return T.unknown();
        args.insert(args.begin(), src);
        native("TA.setFrom");
        return T.voidT();
      }
      if (!big && (meth("indexOf") || meth("includes"))) {
        if (name == "indexOf" && args.size() == 1) args.push_back(N(0));
        if (!argsMatch(e, args, name == "indexOf" ? std::vector<Type*>{num, num} : std::vector<Type*>{num}, name == "indexOf" ? 2 : 1, name.c_str()))
          return T.unknown();
        native(name == "indexOf" ? "TA.indexOf" : "TA.includes");
        return name == "indexOf" ? num : boo;
      }
      if (meth("join") || meth("toString")) {
        if (name == "toString" && !args.empty()) return errT(l, "toString takes no arguments");
        if (args.empty()) args.push_back(S(u","));
        if (!argsMatch(e, args, {str}, 1, "join")) return T.unknown();
        native("TA.join");
        return str;
      }
      if (meth("reverse") || (meth("sort") && args.empty())) {
        if (!argsMatch(e, args, {}, 0, name.c_str())) return T.unknown();
        native(name == "reverse" ? "TA.reverse" : "TA.sort");
        return bt;
      }
      auto comparator = [&](Expr*& x) {
        Type* ft = check(x, T.func({et, et}, num));
        if (ft->t != TT::Func) return errT(x->loc, "sort expects a comparator function");
        if (ft->params.size() != 2) return errT(x->loc, "sort comparator must take two parameters");
        if (ft->ret != num) return errT(x->loc, "sort comparator must return a number");
        coerce(x, T.func({et, et}, num));
        return ft;
      };
      if (meth("sort")) {
        // in place, with the comparator
        if (args.size() != 1) return errT(e->loc, "sort takes at most one argument");
        if (comparator(args[0])->t == TT::Unknown) return T.unknown();
        native("TA.sortCmp");
        return bt;
      }
      if (meth("toSorted")) {
        // a sorted copy: numeric order without a comparator (not the string order of an array)
        if (args.size() > 1) return errT(e->loc, "toSorted takes at most one argument");
        bool withCmp = !args.empty();
        if (withCmp && comparator(args[0])->t == TT::Unknown) return T.unknown();
        Expr* copy = P.arena.expr(EK::Call, m->a->loc);
        copy->list = {m->a, N(0), N(INFINITY)};
        for (size_t i = 1; i < copy->list.size(); i++) check(copy->list[i], num);
        copy->native = "TA.slice";
        copy->intrinsic = IN_NATIVE_NORECV;
        copy->type = bt;
        e->list.insert(e->list.begin(), copy);
        e->a = nullptr;
        e->native = withCmp ? "TA.sortCmp" : "TA.sort";
        e->intrinsic = IN_NATIVE_NORECV;
        return bt;
      }
      // the other methods (callbacks, at, ...): on the elements as an array; map / filter / toSorted /
      // toReversed give the typed array again
      {
        // the callbacks get the elements, not the typed array itself
        size_t maxParams = name == "reduce" || name == "reduceRight" ? 3 : 2;
        for (Expr* a : args) {
          Expr* x = strip(a);
          if (x->k == EK::Arrow && x->params.size() > maxParams)
            return errT(x->params[maxParams].loc, "the callbacks of a " + typeName(bt) + "'s '" + name +
                                                              "' do not get the typed array as a parameter; use the variable that holds it");
        }
        Expr* recv = m->a;
        Expr* conv = P.arena.expr(EK::Call, recv->loc);
        conv->list = {recv};
        conv->native = "TA.toArray";
        conv->intrinsic = IN_NATIVE_NORECV;
        conv->type = T.array(et);
        m->a = conv;
        bool wrap = isCall && (name == "map" || name == "filter" || name == "toSorted" || name == "toReversed" || name == "with" ||
                               (name == "sort" && !args.empty()));
        Type* r = checkBuiltinMethod(e, m, T.array(et), wrap ? T.array(et) : expected);
        if (!wrap || r->t == TT::Unknown) return r;
        if (r->t != TT::Array || baseLit(r->elem) != et) return errT(l, "'" + name + "' on a " + typeName(bt) + " must give " + typeName(et) + "s");
        // e becomes TA.from(kind, <the array call>)
        Expr* inner = P.arena.expr(EK::Call, e->loc);
        *inner = *e;
        inner->type = r;
        Expr saved = *e;
        e->list = {intLit(bt->vt, l), inner};
        e->a = nullptr;
        e->native = "TA.from";
        e->intrinsic = IN_NATIVE_NORECV;
        (void)saved;
        return bt;
      }
    }
    case TT::Promise: {
      Type* vt = bt->elem;
      std::vector<Type*> valueParams;
      if (vt->t != TT::Void) valueParams.push_back(vt);
      Type* errFn = nullptr;
      // a callback (also a function passed by name) must take what the promise gives it: no more
      // parameters, of types that accept the value (with its representation)
      auto cbOk = [&](Expr* a, Type* ft, const std::vector<Type*>& given) {
        if (ft->t != TT::Func) return true;
        if (ft->restParam) { err(a->loc, "a promise callback cannot have a rest parameter"); return false; }
        if (ft->params.size() > given.size()) {
          err(a->loc, "this promise callback takes " + std::to_string(ft->params.size()) + " parameters; it is called with " +
                          std::to_string(given.size()));
          return false;
        }
        for (size_t i = 0; i < ft->params.size(); i++) {
          Type* p = ft->params[i];
          if (p == given[i] || (kindOf(p) == kindOf(given[i]) && !isBoxedOptional(p) && assignable(given[i], p))) continue;
          err(a->loc, "the promise callback's parameter of type '" + typeName(p) + "' does not accept the value of type '" +
                          typeName(given[i]) + "'");
          return false;
        }
        return true;
      };
      if (meth("then")) {
        if (args.empty() || args.size() > 2) return errT(l, "then takes one or two callbacks");
        Type* f1 = check(args[0], T.func(valueParams, T.unknown()));
        if (f1->t != TT::Func) return errT(args[0]->loc, "then expects a function");
        cbOk(args[0], f1, valueParams);
        Type* u = promiseCallbackRet(args[0], f1->ret, args[0]->loc);
        if (args.size() == 2) {
          errFn = check(args[1], T.func({T.error()}, u->t == TT::Void ? T.voidT() : u));
          if (errFn->t != TT::Func) return errT(args[1]->loc, "then expects a function");
          cbOk(args[1], errFn, {T.error()});
          Type* u2 = promiseCallbackRet(args[1], errFn->ret, args[1]->loc);
          if (u2 != u && !(u2->t == TT::Void && u->t == TT::Void))
            return errT(args[1]->loc, "the callbacks of then must return the same type ('" + typeName(u) + "' and '" + typeName(u2) + "')");
        }
        e->intrinsic = IN_PROMISE_THEN;
        e->raw = "then";
        return T.promise(u);
      }
      if (meth("catch")) {
        if (args.size() != 1) return errT(l, "catch takes one callback");
        Type* f2 = check(args[0], T.func({T.error()}, vt));
        if (f2->t != TT::Func) return errT(args[0]->loc, "catch expects a function");
        cbOk(args[0], f2, {T.error()});
        Type* u = promiseCallbackRet(args[0], f2->ret, args[0]->loc);
        // a handler that returns nothing: the promise keeps the type (an e7 limit: undefined is not a T)
        if (u != vt && u->t != TT::Void)
          return errT(args[0]->loc, "the catch callback must return '" + typeName(vt) + "' (the promise's type), not '" + typeName(u) + "'");
        e->intrinsic = IN_PROMISE_THEN;
        e->raw = "catch";
        return T.promise(vt);
      }
      if (meth("finally")) {
        if (args.size() != 1) return errT(l, "finally takes one callback");
        // its result is awaited (a rejected promise rejects the result): not checked as void
        Type* f3 = check(args[0], T.func({}, T.unknown()));
        if (f3->t != TT::Func || !f3->params.empty()) return errT(args[0]->loc, "finally expects a function without parameters");
        e->intrinsic = IN_PROMISE_THEN;
        e->raw = "finally";
        return T.promise(vt);
      }
      return noSuch();
    }
    case TT::AsyncGenerator: {
      // g.next(v?) / g.return(v?) / g.throw(e): a promise of an IteratorResult record. Its class
      // has a presence mask also when `value` is a reference T: a finished generator's value is
      // undefined, a property that is there (printed `value: undefined`, as in JavaScript)
      auto asyncIterResultClass = [&](Type* rt) {
        Type* vt = rt->shape->props[0].type;
        if (kindOf(vt) == K_R && vt->t != TT::Optional && vt->t != TT::Undefined) vt = T.optional(vt, false, true);
        return recordFor({{"value", vt}, {"done", T.boolean()}})->cls;
      };
      if (meth("next") || meth("return")) {
        bool isNext = name == "next";
        Type* want = isNext ? bt->val : bt->ret;
        if (args.size() > 1) return errT(l, "'" + name + "' takes at most one argument");
        if (args.size() == 1) {
          if (!want || want->t == TT::Void) {
            Type* at = check(args[0], nullptr);
            if (at->t != TT::Undefined)
              return errT(args[0]->loc, std::string(isNext ? "this generator receives no value from next()" : "this generator returns no value") +
                                             " (" + typeName(bt) + ")");
            args.clear();
          } else {
            check(args[0], want);
            coerce(args[0], want, isNext ? "next() value" : "return() value");
          }
        }
        e->intrinsic = isNext ? IN_GEN_NEXT : IN_GEN_RETURN;
        Type* rt = iterResultType(bt, l);
        e->cls = asyncIterResultClass(rt);
        return T.promise(rt);
      }
      if (meth("throw")) {
        if (!argsMatch(e, args, {T.error()}, 1, "throw")) return T.unknown();
        e->intrinsic = IN_GEN_THROW;
        Type* rt = iterResultType(bt, l);
        e->cls = asyncIterResultClass(rt);
        return T.promise(rt);
      }
      return noSuch();
    }
    case TT::Generator: {
      // g.next(v?) / g.return(v?) / g.throw(e): an IteratorResult record
      if (meth("next") || meth("return")) {
        bool isNext = name == "next";
        Type* want = isNext ? bt->val : bt->ret;
        if (args.size() > 1) return errT(l, "'" + name + "' takes at most one argument");
        if (args.size() == 1) {
          if (!want || want->t == TT::Void) {
            Type* at = check(args[0], nullptr);
            if (at->t != TT::Undefined)
              return errT(args[0]->loc, std::string(isNext ? "this generator receives no value from next()" : "this generator returns no value") +
                                             " (" + typeName(bt) + ")");
            args.clear();
          } else {
            check(args[0], want);
            coerce(args[0], want, isNext ? "next() value" : "return() value");
          }
        }
        e->intrinsic = isNext ? IN_GEN_NEXT : IN_GEN_RETURN;
        Type* rt = iterResultType(bt, l);
        e->cls = recordFor({{"value", rt->shape->props[0].type}, {"done", T.boolean()}})->cls;
        return rt;
      }
      if (meth("throw")) {
        if (!argsMatch(e, args, {T.error()}, 1, "throw")) return T.unknown();
        e->intrinsic = IN_GEN_THROW;
        Type* rt = iterResultType(bt, l);
        e->cls = recordFor({{"value", rt->shape->props[0].type}, {"done", T.boolean()}})->cls;
        return rt;
      }
      return noSuch();
    }
    case TT::Map: {
      std::string K = kindSuffix(bt->key), V = kindSuffix(bt->val);
      if (prop("size")) { native("Map.size"); return num; }
      if (meth("get")) {
        if (!argsMatch(e, args, {bt->key}, 1, "get")) return T.unknown();
        native("Map.get" + K);
        return T.optional(bt->val, false, true);
      }
      if (meth("set")) {
        if (!argsMatch(e, args, {bt->key, bt->val}, 2, "set")) return T.unknown();
        native("Map.set" + K + V);
        return bt;
      }
      if (meth("has") || meth("delete")) {
        if (!argsMatch(e, args, {bt->key}, 1, name.c_str())) return T.unknown();
        native("Map." + name + K);
        return boo;
      }
      if (meth("clear")) {
        if (!argsMatch(e, args, {}, 0, "clear")) return T.unknown();
        native("Map.clear");
        return T.voidT();
      }
      if (meth("keys") || meth("values")) {
        if (!argsMatch(e, args, {}, 0, name.c_str())) return T.unknown();
        native("Map." + name);
        e->iterCall = true;
        iterCalls.push_back(e);
        return T.array(name == "keys" ? bt->key : bt->val);
      }
      if (meth("entries")) {
        if (!argsMatch(e, args, {}, 0, "entries")) return T.unknown();
        e->iterCall = true;
        iterCalls.push_back(e);
        e->intrinsic = IN_ARRAY_FROM;
        e->cls = tupleRecord(T.tuple({bt->key, bt->val}));
        return T.array(T.tuple({bt->key, bt->val}));
      }
      if (meth("forEach")) {
        if (args.size() != 1) return errT(e->loc, "forEach expects one callback");
        Type* ft = cb(args[0], {bt->val, bt->key, bt}, nullptr);
        if (ft->t != TT::Func) return T.unknown();
        e->intrinsic = IN_MAP_FOREACH;
        return T.voidT();
      }
      return noSuch();
    }
    case TT::Set: {
      std::string K = kindSuffix(bt->elem);
      if (prop("size")) { native("Map.size"); return num; }
      if (meth("add")) {
        if (!argsMatch(e, args, {bt->elem}, 1, "add")) return T.unknown();
        native("Set.add" + K);
        return bt;
      }
      if (meth("has") || meth("delete")) {
        if (!argsMatch(e, args, {bt->elem}, 1, name.c_str())) return T.unknown();
        native("Map." + name + K);
        return boo;
      }
      if (meth("clear")) {
        if (!argsMatch(e, args, {}, 0, "clear")) return T.unknown();
        native("Map.clear");
        return T.voidT();
      }
      if (meth("values") || meth("keys")) {
        if (!argsMatch(e, args, {}, 0, name.c_str())) return T.unknown();
        native("Map.keys");
        e->iterCall = true;
        iterCalls.push_back(e);
        return T.array(bt->elem);
      }
      if (meth("forEach")) {
        if (args.size() != 1) return errT(e->loc, "forEach expects one callback");
        Type* ft = cb(args[0], {bt->elem, bt->elem, bt}, nullptr);
        if (ft->t != TT::Func) return T.unknown();
        e->intrinsic = IN_SET_FOREACH;
        return T.voidT();
      }
      return noSuch();
    }
    case TT::TextEncoder: {
      if (meth("encode")) {
        if (!argsMatch(e, args, {str}, 1, "encode")) return T.unknown();
        e->native = "Text.encode";
        e->intrinsic = IN_NATIVE_NORECV;
        return T.bytes();
      }
      if (prop("encoding")) { e->intrinsic = IN_CONST_STR; e->str = u"utf-8"; return str; }
      return noSuch();
    }
    case TT::TextDecoder: {
      if (meth("decode")) {
        if (!argsMatch(e, args, {T.bytes()}, 1, "decode")) return T.unknown();
        e->native = "Text.decode";
        e->intrinsic = IN_NATIVE_NORECV;
        return str;
      }
      if (prop("encoding")) { e->intrinsic = IN_CONST_STR; e->str = u"utf-8"; return str; }
      return noSuch();
    }
    case TT::Error: {
      if (prop("message")) { native("Error.message"); return str; }
      if (prop("name")) { native("Error.name"); return str; }
      if (prop("cause")) { native("Error.cause"); return T.optional(T.error(), false, true); }
      if (prop("errors")) { native("Error.errors"); return T.array(T.error()); }
      if (prop("stack")) return errT(e->loc, "Error.stack is not supported (stack traces differ between engines)");
      if (meth("toString")) {
        if (!argsMatch(e, args, {}, 0, "toString")) return T.unknown();
        native("Error.toString");
        return str;
      }
      return noSuch();
    }
    case TT::Date: {
      static const char* getters[] = {"getFullYear", "getMonth", "getDate", "getDay", "getHours", "getMinutes",
                                      "getSeconds", "getMilliseconds"};
      for (int k = 0; k < 8; k++) {
        std::string g = getters[k];
        std::string ug = "getUTC" + g.substr(3);
        if (meth(g.c_str()) || meth(ug.c_str())) {
          if (!argsMatch(e, args, {}, 0, name.c_str())) return T.unknown();
          args.push_back(N(k));
          native("Date.get");
          return num;
        }
      }
      if (meth("getTime") || meth("valueOf")) {
        if (!argsMatch(e, args, {}, 0, name.c_str())) return T.unknown();
        native("Date.getTime");
        return num;
      }
      if (meth("getTimezoneOffset")) {
        if (!argsMatch(e, args, {}, 0, name.c_str())) return T.unknown();
        e->intrinsic = IN_CONST_NUM;
        e->num = 0;
        return num;
      }
      {
        static const char* setters[] = {"setFullYear", "setMonth", "setDate", "setHours", "setMinutes", "setSeconds",
                                        "setMilliseconds"};
        static const int maxArgs[] = {3, 2, 1, 4, 3, 2, 1};
        for (int k = 0; k < 7; k++) {
          std::string sn = setters[k];
          std::string us = "setUTC" + sn.substr(3);
          if (!meth(sn.c_str()) && !meth(us.c_str())) continue;
          if (args.empty() || (int)args.size() > maxArgs[k])
            return errT(l, "'" + name + "' takes 1 to " + std::to_string(maxArgs[k]) + " argument(s)");
          size_t given = args.size();
          while (args.size() < 4) args.push_back(N(0));
          if (!argsMatch(e, args, {num, num, num, num}, 4, name.c_str())) return T.unknown();
          args.insert(args.begin(), N(k));
          args.push_back(N((double)given));
          native("Date.set");
          return num;
        }
      }
      if (meth("setTime")) {
        if (!argsMatch(e, args, {num}, 1, "setTime")) return T.unknown();
        native("Date.setTime");
        return num;
      }
      if (meth("toISOString") || meth("toJSON") || meth("toString") || meth("toUTCString") || meth("toDateString") ||
          meth("toTimeString") || meth("toGMTString")) {
        if (!argsMatch(e, args, {}, 0, name.c_str())) return T.unknown();
        std::string n = name == "toJSON" ? "toISOString" : name == "toGMTString" ? "toUTCString" : name;
        native("Date." + n);
        return str;
      }
      return noSuch();
    }
    case TT::Func:
      return errT(m->loc, "function values have no properties");
    default:
      return noSuch();
  }
}

}  // namespace e7

namespace e7 {
// The value type a promise callback produces: U for a callback returning U or Promise<U>
Type* Checker::promiseCallbackRet(Expr* cb, Type* ret, Loc l) {
  (void)cb;
  (void)l;
  if (ret->t == TT::Promise) return ret->elem;
  if (ret->t == TT::Undefined) return T.voidT();
  return ret;
}
}  // namespace e7
