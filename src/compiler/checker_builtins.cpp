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
void Checker::checkArgs(Expr* call, Type* ft, std::vector<Expr*>& args, size_t skip) {
  (void)skip;
  if (!ft || ft->t != TT::Func) return;
  size_t np = ft->params.size();
  size_t minArgs = 0;
  for (size_t i = 0; i < np; i++)
    if (!ft->optParams[i] && !(ft->restParam && i + 1 == np)) minArgs = i + 1;
  if (ft->restParam) {
    // f(a, b, ...rest): extra args are packed into an array; or f(a, ...arr)
    size_t fixed = np - 1;
    if (args.size() < minArgs) { err(call->loc, "expected at least " + std::to_string(minArgs) + " argument(s), got " + std::to_string(args.size())); return; }
    for (size_t i = 0; i < args.size() && i < fixed; i++) {
      if (args[i]->k == EK::Spread) { err(args[i]->loc, "spread argument must match the rest parameter"); return; }
      check(args[i], ft->params[i]);
      coerce(args[i], ft->params[i]);
    }
    Type* restT = ft->params.back();
    if (args.size() == fixed + 1 && args[fixed]->k == EK::Spread) {
      Type* st = check(args[fixed]->a, restT);
      args[fixed]->type = st;
      if (st != restT) err(args[fixed]->loc, "spread argument of type '" + typeName(st) + "' does not match '" + typeName(restT) + "'");
      call->intrinsic = call->intrinsic;  // spread passes array directly
      call->bval = true;
      return;
    }
    // pack extra args into an array literal
    Expr* arr = P.arena.expr(EK::Array, call->loc);
    for (size_t i = fixed; i < args.size(); i++) {
      if (args[i]->k == EK::Spread) { err(args[i]->loc, "spread must be the only rest argument"); return; }
      arr->list.push_back(args[i]);
    }
    check(arr, restT);
    args.resize(fixed);
    args.push_back(arr);
    call->bval = true;  // rest packed
    return;
  }
  if (args.size() < minArgs || args.size() > np) {
    std::string exp = minArgs == np ? std::to_string(np) : std::to_string(minArgs) + "-" + std::to_string(np);
    err(call->loc, "expected " + exp + " argument(s), got " + std::to_string(args.size()));
    return;
  }
  for (size_t i = 0; i < args.size(); i++) {
    if (args[i]->k == EK::Spread) { err(args[i]->loc, "spread arguments require a rest parameter"); return; }
    Type* pt = ft->params[i];
    check(args[i], pt);
    coerce(args[i], pt);
  }
}

// ------------------------------------------------------------------ calls
Type* Checker::checkCall(Expr* e, Type* expected) {
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
    checkArgs(e, pc->type, e->list);
    e->callee = pc;
    e->intrinsic = IN_SUPER_CTOR;
    Sym* th = lookup("this");
    if (th) { e->var = static_cast<VarSym*>(th); markCapture(e->var); }
    return T.voidT();
  }
  if (c->k == EK::Ident) {
    Sym* s = lookup(c->raw);
    if (!s) return errT(c->loc, "cannot find name '" + c->raw + "'" + suggestName(c->raw, false));
    if (s->k == SymK::Builtin) return checkBuiltinCall(e, static_cast<BuiltinSym*>(s)->id, expected);
    if (s->k == SymK::Namespace && static_cast<NamespaceSym*>(s)->builtin) {
      // Number(x), String(x), Boolean(x), BigInt(x)
      return checkBuiltinCall(e, static_cast<NamespaceSym*>(s)->module + "()", expected);
    }
    if (s->k == SymK::Func) {
      FuncSym* f = static_cast<FuncSym*>(s);
      resolveSig(f);
      checkArgs(e, f->type, e->list);
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
    if (s && s->k == SymK::Class) {
      ClassSym* c = static_cast<ClassSym*>(s);
      resolveClass(c);
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
        return errT(e->loc, "class '" + c->name + "' has no static method '" + name + "'");
      }
      FuncSym* f = it->second;
      resolveSig(f);
      checkArgs(e, f->type, e->list);
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
    checkArgs(e, target->type, e->list);
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
    if (!m->optional) return errT(m->loc, "object is possibly null or undefined; use '?.' or a check");
    bt = ot->elem;
    e->optional = true;
  }
  Type* ret = nullptr;
  switch (bt->t) {
    case TT::Class: {
      ClassSym* c = bt->cls;
      resolveClass(c);
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
      if (meth("match") || meth("matchAll") || meth("search") || meth("normalize"))
        return errT(m->loc, "'" + name + "' is not supported (no regular expressions / normalization)");
      return noSuch();
    }
    case TT::Number:
    case TT::Enum: {
      if (bt->t == TT::Enum && bt->en->isString) {
        if (meth("toString")) { e->intrinsic = IN_TOSTRING; return str; }
        return noSuch();
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
        return bt;
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
        if (args.empty()) { native("Array.slice"); args.push_back(N(0)); args.push_back(N(INFINITY)); check(args[0], num); check(args[1], num); return bt; }
        for (Expr*& a : args) {
          Type* t = check(a, bt);
          if (t != bt) return errT(a->loc, "concat arguments must be arrays of type '" + typeName(bt) + "'");
        }
        native("Array.concat");
        return bt;
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
        if (name == "filter") { e->intrinsic = IN_ARR_FILTER; return bt; }
        if (name == "forEach") { e->intrinsic = IN_ARR_FOREACH; return T.voidT(); }
        if (name == "some") { e->intrinsic = IN_ARR_SOME; return boo; }
        if (name == "every") { e->intrinsic = IN_ARR_EVERY; return boo; }
        if (name == "find") { e->intrinsic = IN_ARR_FIND; return T.optional(et, false, true); }
        if (name == "findLast") { e->intrinsic = IN_ARR_FINDLAST; return T.optional(et, false, true); }
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
      if (meth("flat") || meth("flatMap") || meth("copyWithin"))
        return errT(m->loc, "'" + name + "' is not supported");
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
        return T.array(name == "keys" ? bt->key : bt->val);
      }
      if (meth("entries")) {
        if (!argsMatch(e, args, {}, 0, "entries")) return T.unknown();
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
