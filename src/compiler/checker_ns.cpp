// Checker: builtin namespaces (Math, JSON, ...), global builtin functions, `new`.
#include <set>
#include <cmath>

#include "checker.h"
#include "intrinsics.h"
#include "suggest.h"

namespace e7 {

static Expr* strip(Expr* e) {
  while (e && e->k == EK::Paren) e = e->a;
  return e;
}
static Expr* mkNum(Program& P, Types& T, double v, Loc l) {
  Expr* e = P.arena.expr(EK::Num, l);
  e->num = v;
  e->type = T.number();
  return e;
}
static Expr* mkNull(Program& P, Types& T, Loc l) {
  Expr* e = P.arena.expr(EK::Null, l);
  e->type = T.nullT();
  return e;
}

static const struct { const char* name; double v; } kMathConsts[] = {
    {"PI", 3.141592653589793}, {"E", 2.718281828459045}, {"LN2", 0.6931471805599453}, {"LN10", 2.302585092994046},
    {"LOG2E", 1.4426950408889634}, {"LOG10E", 0.4342944819032518}, {"SQRT2", 1.4142135623730951},
    {"SQRT1_2", 0.7071067811865476}};
static const struct { const char* name; double v; } kNumberConsts[] = {
    {"MAX_SAFE_INTEGER", 9007199254740991.0}, {"MIN_SAFE_INTEGER", -9007199254740991.0},
    {"EPSILON", 2.220446049250313e-16}, {"MAX_VALUE", 1.7976931348623157e308}, {"MIN_VALUE", 5e-324},
    {"POSITIVE_INFINITY", INFINITY}, {"NEGATIVE_INFINITY", -INFINITY}, {"NaN", NAN}};

static const char* kMath1[] = {"abs", "ceil", "floor", "round", "trunc", "sign", "sqrt", "cbrt", "exp", "expm1", "log",
                               "log1p", "log2", "log10", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh",
                               "tanh", "asinh", "acosh", "atanh", "fround", "clz32"};
static const char* kMath2[] = {"atan2", "pow", "imul"};

// Does a value of type t contain an Error (directly, or in an element, field, entry or tuple slot)?
static bool containsErrorRec(Type* t, std::set<const void*>& seen) {
  if (!t) return false;
  switch (t->t) {
    case TT::Error: case TT::Caught: return true;
    case TT::Array: case TT::Set: case TT::Optional: return containsErrorRec(t->elem, seen);
    case TT::Map: return containsErrorRec(t->key, seen) || containsErrorRec(t->val, seen);
    case TT::Tuple:
      for (Type* e : t->elems) if (containsErrorRec(e, seen)) return true;
      return false;
    case TT::Object:
      if (!t->shape || !seen.insert(t->shape).second) return false;
      for (auto& p : t->shape->props) if (containsErrorRec(p.type, seen)) return true;
      return false;
    case TT::Class:
      if (!t->cls || !seen.insert(t->cls).second) return false;
      for (auto& f : t->cls->fields) if (containsErrorRec(f.type, seen)) return true;
      return false;
    default: return false;
  }
}
static bool containsError(Type* t) {
  std::set<const void*> seen;
  return containsErrorRec(t, seen);
}

Type* Checker::checkNamespaceMember(Expr* e, NamespaceSym* ns, const std::string& name, bool isCall, Type* expected) {
  (void)expected;
  Loc l = e->loc;
  Type* num = T.number();
  Type* str = T.string();
  Type* bytes = T.bytes();
  std::vector<Expr*>& args = e->list;
  auto nat = [&](const std::string& n) {
    e->native = n;
    e->intrinsic = IN_NATIVE_NORECV;
  };
  auto N = [&](double v) { return mkNum(P, T, v, l); };
  auto bad = [&]() -> Type* {
    std::string hint = suggestNamespaceMember(ns->module, name);
    if (!hint.empty()) return errT(l, "'" + ns->name + "." + name + "' does not exist" + hint);
    return errT(l, "'" + ns->name + "." + name + "' is not supported" + (isCall ? "" : " as a value"));
  };
  if (!ns->builtin) {
    // file module namespace
    auto it = ns->members.find(name);
    if (it == ns->members.end()) {
      std::vector<std::string> names;
      for (auto& kv : ns->members) names.push_back(kv.first);
      return errT(l, "module '" + ns->name + "' has no exported member '" + name + "'" + didYouMean(name, names));
    }
    Sym* s = it->second;
    if (s->k == SymK::Generic && isCall &&
        (static_cast<GenericSym*>(s)->kind == GenericSym::Func || static_cast<GenericSym*>(s)->kind == GenericSym::Arrow)) {
      GenericSym* g = static_cast<GenericSym*>(s);
      std::vector<Type*> targs;
      if (!inferCall(g, g->fnParams(), g->fnRet(), args, e->typeArgs, expected, l, targs)) return T.unknown();
      FuncSym* f = instantiateFunc(g, targs, l);
      if (!f) return T.unknown();
      checkArgs(e, f->type, args);
      if (f->inferRet && !f->checking) checkFunction(f);
      e->callee = f;
      e->intrinsic = IN_DIRECT;
      return f->type->ret;
    }
    if (s->k == SymK::Generic) return errT(l, "'" + name + "' is generic: call it, or use it as a type with type arguments");
    if (s->k == SymK::Builtin) {
      // a function of a builtin module that the module imported or re-exported (export { sha256 } from "e7:crypto")
      if (isCall) return checkBuiltinCall(e, static_cast<BuiltinSym*>(s)->id, expected);
      return errT(l, "builtin function '" + ns->name + "." + name + "' cannot be used as a value; wrap it in an arrow function");
    }
    if (s->k == SymK::Func) {
      FuncSym* f = static_cast<FuncSym*>(s);
      resolveSig(f);
      if (isCall) {
        checkArgs(e, f->type, args);
        if (f->inferRet && !f->checking) checkFunction(f);
        e->callee = f;
        e->intrinsic = IN_DIRECT;
        if (f->isNative) e->native = f->name;
        return f->type->ret;
      }
      if (f->inferRet && !f->checking) checkFunction(f);
      e->callee = f;
      e->intrinsic = IN_DIRECT;
      return f->type;
    }
    if (s->k == SymK::Var) {
      VarSym* v = static_cast<VarSym*>(s);
      Type* t = globalVarType(v);
      if (isCall) {
        if (t->t != TT::Func) return errT(l, "'" + name + "' is not callable");
        Expr* m = e->a;
        m->intrinsic = IN_STATIC_FIELD;
        m->var = v;
        m->type = t;
        checkArgs(e, t, args);
        e->intrinsic = IN_CLOSURE_CALL;
        return t->ret;
      }
      e->intrinsic = IN_STATIC_FIELD;
      e->var = v;
      return t;
    }
    return errT(l, "'" + name + "' is not a value");
  }
  const std::string& M = ns->module;
  if (M == "Math") {
    if (!isCall) {
      for (auto& c : kMathConsts)
        if (name == c.name) { e->intrinsic = IN_CONST_NUM; e->num = c.v; return num; }
      return bad();
    }
    for (const char* f : kMath1)
      if (name == f) {
        if (!argsMatch(e, args, {num}, 1, name.c_str())) return T.unknown();
        nat("Math." + name);
        return num;
      }
    for (const char* f : kMath2)
      if (name == f) {
        if (!argsMatch(e, args, {num, num}, 2, name.c_str())) return T.unknown();
        nat("Math." + name);
        return num;
      }
    if (name == "random") {
      if (!argsMatch(e, args, {}, 0, "random")) return T.unknown();
      nat("Math.random");
      return num;
    }
    if (name == "min" || name == "max" || name == "hypot") {
      for (Expr*& a : args) {
        if (a->k == EK::Spread) {
          Type* st = check(a->a, T.array(num));
          iterUse(a->a);
          // number[] (also readonly, or of numeric literal types: the same values)
          if (st != T.array(num) && !(st->t == TT::Array && st->elem && baseLit(st->elem) == num))
            return errT(a->loc, "spread argument must be number[]");
          a->type = st;
          continue;
        }
        check(a, num);
        if (!coerce(a, num)) return T.unknown();
      }
      e->intrinsic = name == "hypot" ? IN_HYPOT : IN_MATH_MINMAX;
      e->raw = name;
      return num;
    }
    return bad();
  }
  if (M == "Number") {
    if (!isCall) {
      for (auto& c : kNumberConsts)
        if (name == c.name) { e->intrinsic = IN_CONST_NUM; e->num = c.v; return num; }
      return bad();
    }
    if (name == "isInteger" || name == "isSafeInteger" || name == "isFinite" || name == "isNaN") {
      if (!argsMatch(e, args, {num}, 1, name.c_str())) return T.unknown();
      nat("Number." + name);
      return T.boolean();
    }
    if (name == "parseFloat") {
      if (!argsMatch(e, args, {str}, 1, name.c_str())) return T.unknown();
      nat("Global.parseFloat");
      return num;
    }
    if (name == "parseInt") {
      optionalArg(args, 1, N(0), N(0));
      if (!argsMatch(e, args, {str, num}, 2, name.c_str())) return T.unknown();
      nat("Global.parseInt");
      return num;
    }
    return bad();
  }
  if (M == "String") {
    if (isCall && (name == "fromCharCode" || name == "fromCodePoint")) {
      Expr* arr = P.arena.expr(EK::Array, l);
      if (args.size() == 1 && args[0]->k == EK::Spread) arr = args[0]->a;
      else arr->list = args;
      Type* t = check(arr, T.array(num));
      if (t != T.array(num)) return errT(l, "'" + name + "' expects numbers");
      args.clear();
      args.push_back(arr);
      nat(name == "fromCharCode" ? "String.fromCharCodes" : "String.fromCodePoints");
      return str;
    }
    return bad();
  }
  if (M == "JSON") {
    if (isCall && name == "stringify") {
      if (args.empty() || args.size() > 3) return errT(l, "JSON.stringify expects (value, null?, indent?)");
      Type* t = check(args[0], nullptr);
      if (t->t == TT::Void || t->t == TT::Func) return errT(args[0]->loc, "value cannot be serialized to JSON");
      if (kindOf(t) != K_R) coerce(args[0], T.optional(t, false, true));  // box primitives
      if (args.size() >= 2) {
        Expr* rep = strip(args[1]);
        if (rep->k != EK::Null && rep->k != EK::Undefined) return errT(args[1]->loc, "JSON.stringify replacer is not supported; pass null");
        args[1]->type = T.nullT();
      }
      if (args.size() == 3) {
        Type* it = check(args[2], nullptr);
        if (it->t == TT::Number) {
          args.erase(args.begin() + 1);
          nat("JSON.stringifyIndent");
          return str;
        }
        if (it->t == TT::String) {
          args.erase(args.begin() + 1);
          nat("JSON.stringifyIndentStr");
          return str;
        }
        return errT(args[2]->loc, "indent must be a number or string");
      }
      if (args.size() == 2) args.pop_back();
      nat("JSON.stringify");
      return str;
    }
    if (isCall && name == "parse") return errT(l, "JSON.parse must be used with a type assertion: JSON.parse(text) as MyType");
    return bad();
  }
  if (M == "console") {
    if (isCall && (name == "log" || name == "info" || name == "debug" || name == "warn" || name == "error" || name == "trace")) {
      for (Expr*& a : args) {
        if (a->k == EK::Spread) return errT(a->loc, "spread is not supported in console.log");
        Type* t = check(a, nullptr);
        iterUse(a);
        if (t->t == TT::Void) return errT(a->loc, "cannot print a void value");
        if (containsError(t))
          return errT(a->loc, "console.log of an Error prints a stack trace in Node.js; print String(e) or e.message instead" +
                                  std::string(t->t == TT::Error || t->t == TT::Caught ? "" : " (this value contains an Error)"));
      }
      e->intrinsic = IN_CONSOLE_LOG;
      e->raw = (name == "warn" || name == "error") ? "error" : "log";
      return T.voidT();
    }
    return bad();
  }
  if (int tk = typedArrayKind(M)) {
    // Int32Array.from(array | iterable) / Int32Array.of(...values) / Int32Array.BYTES_PER_ELEMENT
    Type* tat = T.typedArray((uint8_t)tk);
    Type* et = taElem(tat);
    if (!isCall && name == "BYTES_PER_ELEMENT") {
      static const int sz[11] = {1, 1, 1, 2, 2, 4, 4, 4, 8, 8, 8};
      e->intrinsic = IN_CONST_NUM;
      e->num = sz[tk];
      return num;
    }
    if (isCall && (name == "of" || name == "from")) {
      Expr* arr;
      if (name == "of") {
        arr = P.arena.expr(EK::Array, l);
        arr->list = args;
        check(arr, T.array(et));
      } else {
        if (args.size() != 1) return errT(l, M + ".from takes one argument (mapping functions are not supported; map the array first)");
        Type* t = check(args[0], T.array(et));
        if (t->t == TT::Array) {
          if (baseLit(t->elem) != et) return errT(args[0]->loc, M + ".from needs an array of " + typeName(et) + "s");
          arr = args[0];
        } else if (t->t == TT::TypedArray || t->t == TT::Bytes) {
          arr = args[0];
        } else {
          // an iterable: Array.from it first
          Expr* af = P.arena.expr(EK::Call, l);
          Expr* mem = P.arena.expr(EK::Member, l);
          Expr* id = P.arena.expr(EK::Ident, l);
          id->raw = "Array";
          mem->a = id;
          mem->raw = "from";
          af->a = mem;
          af->list = {args[0]};
          args[0]->type = nullptr;
          Type* at = check(af, T.array(et));
          if (at->t != TT::Array || baseLit(at->elem) != et) return errT(args[0]->loc, M + ".from needs values of type " + typeName(et));
          arr = af;
        }
      }
      args.clear();
      args.push_back(intLit(tk, l));
      args.push_back(arr);
      nat("TA.from");
      return tat;
    }
    return bad();
  }
  if (M == "Promise") {
    Type* expElem = expected && baseOf(expected)->t == TT::Promise ? baseOf(expected)->elem : nullptr;
    e->intrinsic = IN_PROMISE_STATIC;
    e->raw = name;
    if (isCall && name == "resolve") {
      if (args.empty()) return T.promise(T.voidT());
      if (args.size() != 1) return errT(l, "Promise.resolve takes one argument");
      Type* t = check(args[0], expElem);
      if (t->t == TT::Promise) return t;  // the promise itself
      if (t->t == TT::Void) return errT(args[0]->loc, "cannot resolve a promise with a void value");
      if (expElem && t != expElem) {
        coerce(args[0], expElem, "Promise.resolve");
        t = expElem;
      }
      if (t->t == TT::Undefined) t = T.voidT();
      return T.promise(t);
    }
    if (isCall && name == "reject") {
      if (args.size() != 1) return errT(l, "Promise.reject takes one argument (an Error)");
      if (!argsMatch(e, args, {T.error()}, 1, "Promise.reject")) return T.unknown();
      e->intrinsic = IN_PROMISE_STATIC;
      e->raw = name;
      if (e->typeArgs.size() == 1) return T.promise(resolveType(e->typeArgs[0]));  // Promise.reject<T>(err)
      return T.promise(expElem ? expElem : T.voidT());
    }
    if (isCall && (name == "all" || name == "allSettled" || name == "any" || name == "race")) {
      if (args.size() != 1) return errT(l, "Promise." + name + " takes one array of promises");
      Type* want = nullptr;
      if (expElem && name == "all" && expElem->t == TT::Array) want = T.array(T.promise(expElem->elem));
      if (expElem && (name == "any" || name == "race")) want = T.array(T.promise(expElem));
      Type* t = check(args[0], want);
      if (t->t == TT::Tuple) {
        // [p1, p2]: promises of one type
        Type* et = nullptr;
        for (Type* x : t->elems) et = !et || et == x ? x : nullptr;
        if (!et && !t->elems.empty()) return errT(args[0]->loc, "Promise." + name + " over promises of different types is not supported (E7 types are static); give them one type");
        t = T.array(et ? et : T.promise(T.voidT()));
        Expr* arr = P.arena.expr(EK::Array, args[0]->loc);
        arr->list = strip(args[0])->k == EK::Array ? strip(args[0])->list : std::vector<Expr*>{};
        if (strip(args[0])->k != EK::Array) return errT(args[0]->loc, "Promise." + name + " expects an array of promises");
        args[0] = arr;
        check(arr, t);
      }
      if (t->t != TT::Array || t->elem->t != TT::Promise) return errT(args[0]->loc, "Promise." + name + " expects an array of promises, not '" + typeName(t) + "'");
      e->intrinsic = IN_PROMISE_STATIC;
      e->raw = name;
      Type* vt = t->elem->elem;
      if (name == "all") return T.promise(T.array(vt->t == TT::Void ? T.undef() : vt));
      if (name == "allSettled") {
        Type* vopt = T.optional(vt->t == TT::Void ? T.undef() : vt, false, true);
        Type* ropt = T.optional(T.error(), false, true);
        e->cls = recordFor({{"status", T.string()}, {"value", vopt}, {"reason", ropt}})->cls;
        Type* rec = T.anonObject({{"status", T.string(), false}, {"value", vopt, true}, {"reason", ropt, true}});
        return T.promise(T.array(rec));
      }
      return T.promise(vt);
    }
    e->intrinsic = 0;
    return bad();
  }
  if (M == "Object") {
    if (isCall && name == "keys") {
      if (args.size() != 1) return errT(l, "Object.keys expects one argument");
      Type* t = check(args[0], nullptr);
      Type* b = baseOf(t);
      if (t->t == TT::Optional || (b->t != TT::Object && b->t != TT::Class)) return errT(args[0]->loc, "Object.keys requires an object");
      nat("Object.keys");
      return T.array(str);
    }
    if (isCall && (name == "values" || name == "entries")) {
      // the property types of the static type must all be the same (as TypeScript types the result);
      // the properties are enumerated at run time (Object.keys order), see genObjectEnum
      if (args.size() != 1 || args[0]->k == EK::Spread) return errT(l, "Object." + name + " expects one argument");
      Type* t = check(args[0], nullptr);
      if (t->t == TT::Unknown) return t;
      Type* b = baseOf(t);
      if (t->t == TT::Optional || (b->t != TT::Object && b->t != TT::Class))
        return errT(args[0]->loc, "Object." + name + " requires an object");
      std::vector<std::pair<std::string, Type*>> props;
      if (!readableProps(b, args[0]->loc, ("Object." + name).c_str(), props)) return T.unknown();
      if (props.empty()) return errT(args[0]->loc, "Object." + name + " of an object type without properties is not supported");
      Type* vt = nullptr;
      for (auto& p : props) {
        Type* pt = p.second;
        if (pt->t == TT::Optional && pt->hasUndef) pt = pt->hasNull ? T.optional(pt->elem, true, false) : pt->elem;
        if (!vt) vt = pt;
        else if (vt != pt)
          return errT(args[0]->loc, "Object." + name + " needs properties of one type; '" + typeName(b) + "' has '" +
                                        typeName(vt) + "' and '" + typeName(pt) + "' (use Map<string, T> for dynamic keys)");
      }
      e->intrinsic = name == "values" ? IN_OBJECT_VALUES : IN_OBJECT_ENTRIES;
      if (name == "entries") {
        Type* tt = T.tuple({str, vt});
        e->cls = tupleRecord(tt);
        return T.array(tt);
      }
      return T.array(vt);
    }
    if (isCall && name == "assign") {
      if (args.size() < 2) return errT(l, "Object.assign expects a target and at least one source");
      for (Expr* a : args)
        if (a->k == EK::Spread) return errT(a->loc, "spread arguments are not supported for 'Object.assign'");
      Expr* target = strip(args[0]);
      if (target->k == EK::Object) {
        // Object.assign({ ... }, a, b) is the object literal { ..., ...a, ...b }
        for (size_t i = 1; i < args.size(); i++) {
          ObjProp sp;
          sp.loc = args[i]->loc;
          sp.spread = true;
          sp.value = args[i];
          target->props.push_back(sp);
        }
        args.resize(1);
        check(args[0], expected);
        e->intrinsic = IN_NUMBER;  // identity
        e->raw = "identity";
        return args[0]->type;
      }
      // the target keeps its type: it must have every property of the sources
      Type* tt = check(args[0], nullptr);
      if (tt->t == TT::Unknown) return tt;
      Type* tb = baseOf(tt);
      if (tt->t == TT::Optional || (tb->t != TT::Object && tb->t != TT::Class))
        return errT(args[0]->loc, "Object.assign requires an object target");
      if (tb->t == TT::Class) resolveClass(tb->cls);
      for (size_t i = 1; i < args.size(); i++) {
        if (strip(args[i])->k == EK::Cond) {
          // Object.assign(t, c ? a : b): from the copy { ...(c ? a : b) } (the properties of the branch taken)
          Expr* lit = P.arena.expr(EK::Object, args[i]->loc);
          ObjProp sp;
          sp.loc = args[i]->loc;
          sp.spread = true;
          sp.value = args[i];
          lit->props.push_back(sp);
          args[i] = lit;
        }
        Type* st = check(args[i], nullptr);
        if (st->t == TT::Unknown) return st;
        Type* sb = baseOf(st);
        if (st->t == TT::Optional || (sb->t != TT::Object && sb->t != TT::Class))
          return errT(args[i]->loc, "Object.assign sources must be objects");
        std::vector<std::pair<std::string, Type*>> sprops;
        if (!readableProps(sb, args[i]->loc, "Object.assign", sprops)) return T.unknown();
        for (auto& p : sprops) {
          Type* dt = nullptr;
          if (tb->t == TT::Class) {
            auto it = tb->cls->fieldIdx.find(p.first);
            if (it != tb->cls->fieldIdx.end()) dt = tb->cls->fields[it->second].type;
            else if (tb->cls->getterSlot(p.first) >= 0 || tb->cls->setterSlot(p.first) >= 0)
              return errT(args[i]->loc, "Object.assign: '" + p.first + "' is an accessor of the target; assign it directly");
            else dt = tb->cls->abstractProp(p.first);
          } else if (const ObjShape::Prop* tp = tb->shape->findProp(p.first)) {
            dt = tp->type;
          } else if (tb->shape->findMethod("get " + p.first) || tb->shape->findMethod("set " + p.first)) {
            return errT(args[i]->loc, "Object.assign: '" + p.first + "' is an accessor of the target; assign it directly");
          }
          if (!dt)
            return errT(args[i]->loc, "Object.assign: the target type '" + typeName(tb) + "' has no property '" + p.first +
                                          "' (e7 objects cannot gain properties; assign to an object literal: Object.assign({}, ...))");
          Type* sv = p.second;
          Type* svBase = sv->t == TT::Optional && sv->hasUndef ? (sv->hasNull ? T.optional(sv->elem, true, false) : sv->elem) : sv;
          bool ok = sv == dt || svBase == dt ||
                    (kindOf(sv) == K_R && kindOf(dt) == K_R && !isBoxedOptional(sv) && !isBoxedOptional(dt) && assignable(sv, dt));
          if (!ok)
            return errT(args[i]->loc, "Object.assign: property '" + p.first + "' of type '" + typeName(sv) +
                                          "' is not assignable to '" + typeName(dt) + "'");
        }
      }
      e->intrinsic = IN_OBJECT_ASSIGN;
      return tt;
    }
    if (isCall && name == "freeze") {
      if (args.size() != 1) return errT(l, "Object.freeze expects one argument");
      Type* t = check(args[0], nullptr);
      e->intrinsic = IN_NUMBER;  // identity
      e->raw = "identity";
      // as in TypeScript, a frozen array is a readonly array (writes through the result do not compile;
      // e7 does not stop writes through other references: see SPEC section 9)
      if (t->t == TT::Array) return T.array(t->elem, true);
      return t;
    }
    return bad();
  }
  if (M == "Array") {
    if (isCall && name == "from") {
      if (args.size() == 2) {
        // Array.from({ length: n }, (_, i) => ...)
        Expr* o = strip(args[0]);
        if (o->k != EK::Object || o->props.size() != 1 || o->props[0].key != "length" || o->props[0].accessor)
          return errT(l, "Array.from(x, fn) is only supported as Array.from({ length: n }, fn)");
        check(o->props[0].value, num);
        if (!coerce(o->props[0].value, num)) return T.unknown();
        Type* want = expected && baseOf(expected)->t == TT::Array ? baseOf(expected)->elem : nullptr;
        Type* ft = check(args[1], T.func({T.undef(), num}, want ? want : T.unknown()));
        if (ft->t != TT::Func) return errT(args[1]->loc, "expected a function");
        if (ft->params.size() > 2) return errT(args[1]->loc, "callback has too many parameters");
        if (ft->ret->t == TT::Void) return errT(args[1]->loc, "callback must return a value");
        e->intrinsic = IN_ARR_FROM_LENGTH;
        return T.array(ft->ret);
      }
      if (args.size() != 1) return errT(l, "Array.from expects one argument");
      Type* t = check(args[0], nullptr);
      iterUse(args[0]);
      Type* et = elemTypeForIter(args[0], t, args[0]->loc);
      if (t->t == TT::Map) e->cls = tupleRecord(et);
      e->intrinsic = IN_ARRAY_FROM;
      return T.array(et);
    }
    if (isCall && name == "of") {
      Expr* arr = P.arena.expr(EK::Array, l);
      arr->list = args;
      // Array.of<T>(...): an array of T
      Type* t = check(arr, e->typeArgs.size() == 1 ? T.array(resolveType(e->typeArgs[0])) : expected);
      args.clear();
      args.push_back(arr);
      e->intrinsic = IN_NUMBER;
      e->raw = "identity";
      return t;
    }
    if (isCall && name == "isArray") return errT(l, "Array.isArray is not needed in E7 (types are static)");
    return bad();
  }
  if (M == "Uint8Array") {
    if (isCall && (name == "from" || name == "of")) {
      Expr* src;
      if (name == "of") {
        src = P.arena.expr(EK::Array, l);
        src->list = args;
        check(src, T.array(num));
      } else {
        if (args.size() != 1) return errT(l, "Uint8Array.from expects one argument");
        src = args[0];
        Type* t = check(src, T.array(num));
        if (t->t == TT::Bytes) { args.clear(); args.push_back(src); nat("Bytes.copy"); return bytes; }
        if (t != T.array(num)) return errT(src->loc, "Uint8Array.from expects number[] or Uint8Array");
      }
      args.clear();
      args.push_back(src);
      nat("Bytes.fromArray");
      return bytes;
    }
    if (!isCall && name == "BYTES_PER_ELEMENT") { e->intrinsic = IN_CONST_NUM; e->num = 1; return num; }
    return bad();
  }
  if (M == "BigInt") {
    if (isCall && (name == "asUintN" || name == "asIntN")) {
      if (!argsMatch(e, args, {num, T.bigint()}, 2, name.c_str())) return T.unknown();
      nat("BigInt." + name);
      return T.bigint();
    }
    return bad();
  }
  if (M == "Date") {
    if (isCall && name == "now") {
      if (!argsMatch(e, args, {}, 0, "now")) return T.unknown();
      nat("Date.now");
      return num;
    }
    if (isCall && name == "UTC") {
      if (args.empty() || args.size() > 7) return errT(l, "Date.UTC expects 1-7 numbers");
      while (args.size() < 7) args.push_back(N(args.size() == 2 ? 1 : 0));
      if (!argsMatch(e, args, {num, num, num, num, num, num, num}, 7, "Date.UTC")) return T.unknown();
      nat("Date.UTC");
      return num;
    }
    if (isCall && name == "parse") {
      if (!argsMatch(e, args, {str}, 1, "Date.parse")) return T.unknown();
      nat("Date.parse");
      return num;
    }
    return bad();
  }
  if (M == "e7:crypto" || M == "e7:encoding" || M == "e7:json" || M == "e7:test") {
    BuiltinSym tmp;
    std::string id = M + "." + name;
    const auto& names = builtinModuleExports(M);
    if (std::find(names.begin(), names.end(), name) == names.end()) {
      std::string hint = removedBuiltinHint(M, name);
      return errT(l, "module '" + M + "' has no exported member '" + name + "'" + (hint.empty() ? didYouMean(name, names) : hint));
    }
    if (!isCall) return errT(l, "'" + name + "' cannot be used as a value");
    return checkBuiltinCall(e, id, expected);
  }
  return errT(l, "'" + ns->module + "' is not supported in E7");
}

// ------------------------------------------------------------------ global builtin functions
Type* Checker::checkBuiltinCall(Expr* e, const std::string& id, Type* expected) {
  (void)expected;
  Loc l = e->loc;
  std::vector<Expr*>& args = e->list;
  Type* num = T.number();
  Type* str = T.string();
  Type* bytes = T.bytes();
  Type* boo = T.boolean();
  auto nat = [&](const std::string& n) {
    e->native = n;
    e->intrinsic = IN_NATIVE_NORECV;
  };
  auto N = [&](double v) { return mkNum(P, T, v, l); };
  auto one = [&](const char* nm) -> Type* {
    if (args.size() != 1) { err(l, std::string("'") + nm + "' expects 1 argument"); return nullptr; }
    if (args[0]->k == EK::Spread) { err(args[0]->loc, "spread is not supported here"); return nullptr; }
    return check(args[0], nullptr);
  };
  // conversions to integer types
  static const std::pair<const char*, uint8_t> ints[] = {{"i8", VT_I8}, {"u8", VT_U8}, {"i16", VT_I16}, {"u16", VT_U16},
                                                          {"i32", VT_I32}, {"u32", VT_U32}, {"i64", VT_I64}, {"u64", VT_U64},
                                                          {"int", VT_I64}};
  for (auto& p : ints) {
    if (id != p.first) continue;
    Type* target = T.intT(p.second);
    if (args.size() != 1) return errT(l, id + "() expects 1 argument");
    Type* t;
    int64_t litv;
    std::string why;
    Expr* lit = args[0];
    while (lit->k == EK::Paren) lit = lit->a;
    bool neg = lit->k == EK::Unary && lit->raw == "-";
    Expr* num = neg ? lit->a : lit;
    while (num->k == EK::Paren) num = num->a;
    if (isIntLiteralExpr(args[0]) && litFits(num, target, litv, neg, why)) t = check(args[0], target);
    else t = check(args[0], nullptr);
    if (t->t != TT::Number && t->t != TT::Int && t->t != TT::BigInt && t->t != TT::Bool && t->t != TT::Unknown &&
        !(t->t == TT::Enum && !t->en->isString))
      return errT(args[0]->loc, "cannot convert '" + typeName(t) + "' to '" + typeName(target) + "'");
    e->intrinsic = IN_TO_INT;
    return target;
  }
  struct IntOp { const char* name; int arity; int width; };  // width: 32 / 64 / 0 = any
  static const IntOp intOps[] = {{"rotl32", 2, 32}, {"rotr32", 2, 32}, {"rotl64", 2, 64}, {"rotr64", 2, 64},
                                 {"clz32", 1, 32},  {"clz64", 1, 64},  {"ctz32", 1, 32},  {"ctz64", 1, 64},
                                 {"popcnt", 1, 0},  {"bswap16", 1, 16}, {"bswap32", 1, 32}, {"bswap64", 1, 64},
                                 {"mulhi64", 2, 64}, {"imulhi64", 2, 64}};
  for (auto& op : intOps) {
    if (id != op.name) continue;
    if ((int)args.size() != op.arity) return errT(l, id + "() expects " + std::to_string(op.arity) + " argument(s)");
    Type* t0 = check(args[0], nullptr);
    if (t0->t == TT::Unknown) return T.unknown();
    if (t0->t != TT::Int) return errT(args[0]->loc, id + "() requires an integer type argument (e.g. u32)");
    int bits = intBits(t0);
    if (op.width && bits != op.width) return errT(args[0]->loc, id + "() requires a " + std::to_string(op.width) + "-bit integer");
    if (id == "imulhi64" && !isSignedInt(t0)) return errT(args[0]->loc, "imulhi64 requires i64");
    if (id == "mulhi64" && isSignedInt(t0)) return errT(args[0]->loc, "mulhi64 requires u64");
    if (op.arity == 2) {
      Type* t1 = isIntLiteralExpr(args[1]) ? check(args[1], t0) : check(args[1], nullptr);
      bool isRot = id.rfind("rot", 0) == 0;
      if (t1->t != TT::Int && t1->t != TT::Unknown && !(isRot && t1->t == TT::Number))
        return errT(args[1]->loc, id + "() requires an integer argument");
      if ((id == "mulhi64" || id == "imulhi64") && t1 != t0) return errT(args[1]->loc, "operands must have the same type");
    }
    e->intrinsic = IN_INTOP;
    e->raw = id;
    return t0;
  }
  if (id == "parseInt") {
    optionalArg(args, 1, N(0), N(0));  // (a radix that is undefined: 0, decided by the text)
    if (!argsMatch(e, args, {str, num}, 2, "parseInt")) return T.unknown();
    nat("Global.parseInt");
    return num;
  }
  if (id == "structuredClone") {
    // a deep copy (shared and cyclic references kept); classes, functions, generators and promises
    // cannot be cloned (in JavaScript a class instance would become a plain object)
    if (args.size() != 1) return errT(l, "structuredClone takes one argument");
    Type* t = check(args[0], expected);
    std::set<const void*> seen;
    std::function<const char*(Type*)> bad = [&](Type* x) -> const char* {
      if (!x || !seen.insert(x).second) return nullptr;
      switch (x->t) {
        case TT::Class: return x->cls->isRecord ? nullptr : "a class instance (it would become a plain object)";
        case TT::Func: return "a function";
        case TT::Generator: return "a generator";
        case TT::AsyncGenerator: return "an async generator";
        case TT::Promise: return "a promise";
        case TT::Handle: return "a KeyHandle";
        case TT::TextEncoder: case TT::TextDecoder: return "a TextEncoder / TextDecoder";
        case TT::Array: case TT::Set: case TT::Optional: return bad(x->elem);
        case TT::Map: { const char* k = bad(x->key); return k ? k : bad(x->val); }
        case TT::Tuple: for (Type* el : x->elems) if (const char* r = bad(el)) return r; return nullptr;
        case TT::Object:
          for (auto& m : x->shape->methods)
            if (m.name.compare(0, 4, "get ") == 0 || m.name.compare(0, 4, "set ") == 0)
              return "an object with getters or setters (the copy has data properties instead, so it is not of the same type)";
          if (!x->shape->methods.empty()) return "an object with methods";
          for (auto& p : x->shape->props) if (const char* r = bad(p.type)) return r;
          return nullptr;
        default: return nullptr;
      }
    };
    if (const char* why = bad(t)) return errT(args[0]->loc, std::string("structuredClone cannot copy ") + why + " ('" + typeName(t) + "')");
    nat("Global.structuredClone");
    if (kindOf(t) != K_R) e->native = "";  // a primitive is its own copy
    if (kindOf(t) != K_R) e->intrinsic = IN_NUMBER, e->raw = "identity";
    return t;
  }
  if (id == "parseFloat") {
    if (!argsMatch(e, args, {str}, 1, "parseFloat")) return T.unknown();
    nat("Global.parseFloat");
    return num;
  }
  if (id == "isNaN" || id == "isFinite") {
    if (!argsMatch(e, args, {num}, 1, id.c_str())) return T.unknown();
    nat("Number." + id);
    return boo;
  }
  if (id == "encodeURIComponent" || id == "decodeURIComponent" || id == "encodeURI" || id == "decodeURI") {
    if (!argsMatch(e, args, {str}, 1, id.c_str())) return T.unknown();
    nat("Global." + id);
    return str;
  }
  if (id == "idiv" || id == "imod") {
    if (!argsMatch(e, args, {num, num}, 2, id.c_str())) return T.unknown();
    nat("Math." + id);
    return num;
  }
  if (id == "print") {
    for (Expr*& a : args) {
      Type* t = check(a, nullptr);
      if (t->t == TT::Void) return errT(a->loc, "cannot print a void value");
    }
    e->intrinsic = IN_CONSOLE_LOG;
    e->raw = "log";
    return T.voidT();
  }
  if (id == "abort") {
    if (args.size() > 1) return errT(l, "abort expects at most one argument");
    if (args.empty()) args.push_back(P.arena.expr(EK::Str, l)), args[0]->str = u"abort", args[0]->type = str;
    else { check(args[0], str); coerce(args[0], str); }
    e->intrinsic = IN_ABORT;
    return T.never();
  }
  // Number(x) / String(x) / Boolean(x) / BigInt(x)
  if (id == "Number()") {
    Type* t = one("Number");
    if (!t) return T.unknown();
    if (t->t != TT::Number && t->t != TT::String && t->t != TT::Bool && t->t != TT::Int && t->t != TT::BigInt &&
        t->t != TT::Unknown && !(t->t == TT::Enum && !t->en->isString))
      return errT(args[0]->loc, "cannot convert '" + typeName(t) + "' to number");
    e->intrinsic = IN_NUMBER;
    return num;
  }
  if (id == "String()") {
    Type* t = one("String");
    if (!t) return T.unknown();
    if (t->t == TT::Void) return errT(args[0]->loc, "cannot convert void to string");
    e->intrinsic = IN_STRING;
    return str;
  }
  if (id == "Boolean()") {
    if (args.size() != 1) return errT(l, "Boolean expects 1 argument");
    checkCond(args[0]);
    e->intrinsic = IN_BOOLEAN;
    return boo;
  }
  if (id == "BigInt()") {
    Type* t = one("BigInt");
    if (!t) return T.unknown();
    if (t->t != TT::Number && t->t != TT::String && t->t != TT::Int && t->t != TT::BigInt && t->t != TT::Unknown)
      return errT(args[0]->loc, "cannot convert '" + typeName(t) + "' to bigint");
    e->intrinsic = IN_BIGINT;
    return T.bigint();
  }
  if (id == "Array()" && strip(e->a)->k == EK::Ident) {
    // Array<T>(n) / Array(a, b): the same as new Array(...)
    e->k = EK::New;
    return checkNew(e, expected);
  }
  if (id == "Array()" || id == "Map()" || id == "Set()" || id == "Uint8Array()" || id == "Error()")
    return errT(l, "'" + id.substr(0, id.size() - 2) + "' must be called with 'new'");
  // ------------------------------------------------------------ e7:test
  if (id.rfind("e7:test.", 0) == 0) {
    std::string n = id.substr(8);
    Type* optStr = T.optional(str, true, false);
    for (Expr* a : args)
      if (a->k == EK::Spread) return errT(a->loc, "spread arguments are not supported for '" + n + "'");
    auto msgArg = [&](size_t idx) {
      if (args.size() == idx) args.push_back(mkNull(P, T, l));
    };
    if (n == "assert") {
      msgArg(1);
      if (!argsMatch(e, args, {boo, optStr}, 2, "assert")) return T.unknown();
      nat("test.assert");
      return T.voidT();
    }
    if (n == "fail") {
      msgArg(0);
      if (!argsMatch(e, args, {optStr}, 1, "fail")) return T.unknown();
      nat("test.fail");
      return T.never();
    }
    if (n == "later") {
      // later(v): a promise of v settled in a later turn (each one after the previous, as
      // setImmediate does in Node.js): tests of async ordering
      if (args.size() != 1) return errT(l, "'later' expects one argument");
      Type* expElem = expected && baseOf(expected)->t == TT::Promise ? baseOf(expected)->elem : nullptr;
      Type* t = check(args[0], expElem);
      if (expElem && t != expElem) { coerce(args[0], expElem, "later"); t = expElem; }
      if (t->t == TT::Void || t->t == TT::Promise) return errT(args[0]->loc, "'later' needs a value");
      Type* vt = t->t == TT::Undefined ? T.voidT() : t;
      args.push_back(N((double)(t->t == TT::Undefined ? VT_REF : vtOf(t))));
      args.back()->type = T.intT(VT_I64);
      args.back()->k = EK::Num;
      args.back()->isIntLit = true;
      args.back()->ival = t->t == TT::Undefined ? VT_REF : vtOf(t);
      nat(std::string("test.later") + kindChar(kindOf(t)));
      return T.promise(vt);
    }
    if (n == "laterReject") {
      if (args.size() != 1) return errT(l, "'laterReject' expects one argument (an Error)");
      Type* expElem = expected && baseOf(expected)->t == TT::Promise ? baseOf(expected)->elem : T.voidT();
      if (!argsMatch(e, args, {T.error()}, 1, "laterReject")) return T.unknown();
      args.push_back(N((double)(expElem->t == TT::Void ? VT_REF : vtOf(expElem))));
      args.back()->type = T.intT(VT_I64);
      args.back()->isIntLit = true;
      args.back()->ival = expElem->t == TT::Void ? VT_REF : vtOf(expElem);
      nat("test.laterReject");
      return T.promise(expElem);
    }
    if (n == "assertClose") {
      if (args.size() < 2 || args.size() > 4)
        return errT(l, "'assertClose' expects 2-4 argument(s), got " + std::to_string(args.size()));
      if (args.size() == 2) args.push_back(N(1e-9));
      msgArg(3);
      if (!argsMatch(e, args, {num, num, num, optStr}, 4, "assertClose")) return T.unknown();
      nat("test.close");
      return T.voidT();
    }
    if (n == "assertThrows") {
      if (args.empty() || args.size() > 2)
        return errT(l, "'assertThrows' expects 1-2 argument(s), got " + std::to_string(args.size()));
      Type* ft = check(args[0], T.func({}, T.voidT()));
      if (ft->t == TT::Unknown) return T.unknown();
      if (ft->t != TT::Func || !ft->params.empty())
        return errT(args[0]->loc, "'assertThrows' expects a function without parameters, e.g. () => parse(input)");
      msgArg(1);
      if (!argsMatch(e, args, {ft, optStr}, 2, "assertThrows")) return T.unknown();
      nat("test.throws");
      return T.voidT();
    }
    if (n == "assertEqual" || n == "assertNotEqual") {
      if (args.size() < 2 || args.size() > 3)
        return errT(l, "'" + n + "' expects 2-3 argument(s), got " + std::to_string(args.size()));
      // the type of the comparison comes from the first operand, unless it is a literal whose type
      // depends on context (integer literal, null, undefined, empty array): then from the second
      auto contextual = [&](Expr* x) {
        while (x->k == EK::Paren) x = x->a;
        return isIntLiteralExpr(x) || x->k == EK::Null || (x->k == EK::Ident && x->raw == "undefined") ||
               (x->k == EK::Array && x->list.empty());
      };
      size_t anchor = contextual(args[0]) && !contextual(args[1]) ? 1 : 0;
      Type* t = check(args[anchor], nullptr);
      if (t->t == TT::Unknown) return T.unknown();
      if (t->t == TT::Void || t->t == TT::Never) return errT(args[anchor]->loc, "'" + n + "' cannot compare void values");
      if (t->t == TT::Null || t->t == TT::Undefined)
        return errT(args[anchor]->loc, "'" + n + "' needs a typed operand (e.g. a variable), not only null/undefined");
      Type* t2 = check(args[1 - anchor], t);
      if (t2->t == TT::Unknown) return T.unknown();
      if (!coerce(args[1 - anchor], t)) return T.unknown();
      msgArg(2);
      Type* tm = check(args[2], optStr);
      if (tm->t == TT::Unknown) return T.unknown();
      if (!coerce(args[2], optStr)) return T.unknown();
      args.insert(args.begin() + 2, N((double)(vtOf(t) + (n == "assertNotEqual" ? 32 : 0))));
      nat("test.equal" + kindSuffix(t));
      return T.voidT();
    }
    return errT(l, "module 'e7:test' has no function '" + n + "'" + didYouMean(n, builtinModuleExports("e7:test")));
  }
  // ------------------------------------------------------------ e7:json
  // (tryParse(text) as T | null is handled in checkAs; reaching here means the assertion is missing)
  if (id == "e7:json.tryParse")
    return errT(l, "tryParse must be used with a type assertion: tryParse(text) as MyType | null");
  if (id == "e7:json.lastError") {
    if (!argsMatch(e, args, {}, 0, "lastError")) return T.unknown();
    nat("JSON.lastError");
    return str;
  }
  // ------------------------------------------------------------ e7:encoding
  if (id.rfind("e7:encoding.", 0) == 0) {
    std::string n = id.substr(12);
    if (n == "hexEncode" || n == "base64Encode" || n == "base64UrlEncode" || n == "base32Encode" || n == "utf8Decode") {
      if (!argsMatch(e, args, {bytes}, 1, n.c_str())) return T.unknown();
      nat("encoding." + n);
      return str;
    }
    if (n == "hexDecode" || n == "base64Decode" || n == "base64UrlDecode" || n == "base32Decode" || n == "utf8Encode") {
      if (!argsMatch(e, args, {str}, 1, n.c_str())) return T.unknown();
      nat("encoding." + n);
      return bytes;
    }
    if (n == "pemDecode") {
      if (args.size() == 1) args.push_back(mkNull(P, T, l));
      if (!argsMatch(e, args, {str, T.optional(str, true, false)}, 2, n.c_str())) return T.unknown();
      nat("encoding.pemDecode");
      return bytes;
    }
    if (n == "pemEncode") {
      if (!argsMatch(e, args, {bytes, str}, 2, n.c_str())) return T.unknown();
      nat("encoding.pemEncode");
      return str;
    }
    if (n == "concatBytes") {
      Expr* arr = P.arena.expr(EK::Array, l);
      if (args.size() == 1 && args[0]->k == EK::Spread) arr = args[0]->a;
      else arr->list = args;
      Type* t = check(arr, T.array(bytes));
      if (t != T.array(bytes)) return errT(l, "concatBytes expects Uint8Array arguments");
      args.clear();
      args.push_back(arr);
      nat("encoding.concatBytes");
      return bytes;
    }
    if (n == "bytesEqual") {
      if (!argsMatch(e, args, {bytes, bytes}, 2, n.c_str())) return T.unknown();
      nat("encoding.bytesEqual");
      return boo;
    }
    if (n == "crc32") {
      if (!argsMatch(e, args, {bytes}, 1, "crc32")) return T.unknown();
      nat("encoding.crc32");
      return num;
    }
    return errT(l, "module 'e7:encoding' has no function '" + n + "'" + didYouMean(n, builtinModuleExports("e7:encoding")));
  }
  // ------------------------------------------------------------ e7:crypto
  if (id.rfind("e7:crypto.", 0) == 0) {
    std::string n = id.substr(10);
    static const char* hashes[] = {"sha256", "sha384", "sha512"};
    for (const char* h : hashes)
      if (n == h) {
        if (!argsMatch(e, args, {bytes}, 1, h)) return T.unknown();
        nat(std::string("crypto.hash.") + h);
        return bytes;
      }
    Type* handle = T.handle();
    Type* optBytes = T.optional(bytes, true, false);
    auto keyArg = [&](size_t idx) -> int {  // 0 = bytes, 1 = handle, -1 error
      if (idx >= args.size()) return -1;
      Type* t = check(args[idx], nullptr);
      if (t->t == TT::Bytes) return 0;
      if (t->t == TT::Handle) return 1;
      if (t->t == TT::Unknown) return 0;
      err(args[idx]->loc, "key must be a Uint8Array or KeyHandle");
      return -1;
    };
    if (n == "hash") {
      if (!argsMatch(e, args, {str, bytes}, 2, "hash")) return T.unknown();
      nat("crypto.hash");
      return bytes;
    }
    if (n == "hmac") {
      if (args.size() != 3) return errT(l, "hmac expects (alg, key, data)");
      int k = keyArg(1);
      if (k < 0) return T.unknown();
      if (!argsMatch(e, args, {str, k ? handle : bytes, bytes}, 3, "hmac")) return T.unknown();
      nat(k ? "crypto.hmacKey" : "crypto.hmac");
      return bytes;
    }
    if (n == "hkdf") {
      if (!argsMatch(e, args, {str, bytes, bytes, bytes, num}, 5, "hkdf")) return T.unknown();
      nat("crypto.hkdf");
      return bytes;
    }
    if (n == "pbkdf2") {
      if (!argsMatch(e, args, {str, bytes, bytes, num, num}, 5, "pbkdf2")) return T.unknown();
      nat("crypto.pbkdf2");
      return bytes;
    }
    if (n == "randomBytes") {
      if (!argsMatch(e, args, {num}, 1, "randomBytes")) return T.unknown();
      nat("crypto.randomBytes");
      return bytes;
    }
    if (n == "timingSafeEqual") {
      if (!argsMatch(e, args, {bytes, bytes}, 2, n.c_str())) return T.unknown();
      nat("crypto.timingSafeEqual");
      return boo;
    }
    if (n == "aesGcmEncrypt" || n == "aesGcmDecrypt" || n == "chacha20Poly1305Encrypt" || n == "chacha20Poly1305Decrypt") {
      if (args.size() == 3) args.push_back(mkNull(P, T, l));
      if (!argsMatch(e, args, {bytes, bytes, bytes, optBytes}, 4, n.c_str())) return T.unknown();
      nat("crypto." + n);
      return n.find("Decrypt") != std::string::npos ? T.optional(bytes, true, false) : bytes;
    }
    if (n == "ed25519PublicKey") {
      if (!argsMatch(e, args, {bytes}, 1, n.c_str())) return T.unknown();
      nat("crypto.ed25519PublicKey");
      return bytes;
    }
    if (n == "ed25519Sign") {
      if (!argsMatch(e, args, {bytes, bytes}, 2, n.c_str())) return T.unknown();
      nat("crypto.ed25519Sign");
      return bytes;
    }
    if (n == "ed25519Verify") {
      if (!argsMatch(e, args, {bytes, bytes, bytes}, 3, n.c_str())) return T.unknown();
      nat("crypto.ed25519Verify");
      return boo;
    }
    if (n == "x25519") {
      if (!argsMatch(e, args, {bytes, bytes}, 2, n.c_str())) return T.unknown();
      nat("crypto.x25519");
      return bytes;
    }
    return errT(l, "module 'e7:crypto' has no function '" + n + "'" + didYouMean(n, builtinModuleExports("e7:crypto")));
  }
  if (id == "Symbol()" || id.compare(0, 7, "Symbol.") == 0)
    return errT(l, "'Symbol' is not supported (property names are fixed identifiers); use string constants or a private field");
  return errT(l, "unknown builtin '" + id + "'");
}

// ------------------------------------------------------------------ new
Type* Checker::checkNew(Expr* e, Type* expected) {
  Expr* c = strip(e->a);
  Type* exp = expected ? baseOf(expected) : nullptr;
  std::vector<Expr*>& args = e->list;
  Loc l = e->loc;
  Type* num = T.number();
  auto nat = [&](const std::string& n) {
    e->native = n;
    e->intrinsic = IN_NATIVE_NORECV;
  };
  if (c->k != EK::Ident) {
    if (c->k == EK::Member) {
      Expr* base = strip(c->a);
      Sym* bs = base->k == EK::Ident ? lookup(base->raw) : nullptr;
      if (base->k == EK::Ident && base->raw == "Intl" && !bs)
        return errT(l, "'Intl' is not supported (locale data differs between platforms); format with toFixed / padStart, or let the host format");
      if (bs && bs->k == SymK::Namespace && !static_cast<NamespaceSym*>(bs)->builtin) {
        // a class is rewritten to the identifier "ns.C" (resolveNamespaceRefs): this member is something else
        auto& members = static_cast<NamespaceSym*>(bs)->members;
        if (members.count(c->raw)) return errT(l, "'" + base->raw + "." + c->raw + "' is not a class");
        std::vector<std::string> names;
        for (auto& kv : members) names.push_back(kv.first);
        return errT(l, "module '" + base->raw + "' has no exported member '" + c->raw + "'" + didYouMean(c->raw, names));
      }
      if (bs && bs->k == SymK::Namespace)
        return errT(l, "classes cannot be constructed through a namespace import ('new " + base->raw + "." + c->raw +
                           "'); import the class by name");
    }
    return errT(l, "unsupported 'new' expression");
  }
  const std::string& name = c->raw;
  Sym* s = lookup(name);
  if (s && s->k == SymK::Generic && static_cast<GenericSym*>(s)->kind == GenericSym::Class) {
    // new Box<number>(...) / new Box(...): type arguments explicit, or inferred from the constructor
    // arguments, or taken from the expected type
    GenericSym* g = static_cast<GenericSym*>(s);
    std::vector<Param>* ctorParams = nullptr;
    for (auto& m : g->decl->members)
      if (m.kind == ClassMember::Ctor) ctorParams = &m.params;
    std::vector<Type*> targs;
    if (e->typeArgs.empty() && exp && exp->t == TT::Class && exp->cls->origin == g) {
      targs = exp->cls->typeArgs;
    } else if (e->typeArgs.empty() && !ctorParams && g->decl->extends) {
      // inherited constructor: infer the base class's type arguments from the arguments, then this
      // class's from `extends Base<...>`
      TypeNode* ext = g->decl->extends;
      Sym* bs = lookupFrom(g->defScope, ext->name);
      GenericSym* h = bs && bs->k == SymK::Generic ? static_cast<GenericSym*>(bs) : nullptr;
      std::vector<Param>* hp = nullptr;
      if (h && h->kind == GenericSym::Class)
        for (auto& m : h->decl->members)
          if (m.kind == ClassMember::Ctor) hp = &m.params;
      if (!hp)
        return errT(l, "cannot infer the type arguments of '" + name + "' (its constructor is inherited); write new " + name + "<...>(...)");
      std::vector<Type*> hargs;
      if (!inferCall(h, hp, nullptr, args, {}, nullptr, l, hargs)) return T.unknown();
      targs.assign(g->params->size(), nullptr);
      for (size_t j = 0; j < ext->args.size() && j < hargs.size(); j++) inferMatch(g, targs, ext->args[j], hargs[j]);
      int r = completeTypeArgs(g, targs, l);
      if (r >= 0) return errT(l, "cannot infer the type argument '" + (*g->params)[r].name + "' of '" + name + "'; write new " + name + "<...>(...)");
      if (r == -2) return T.unknown();
    } else if (!inferCall(g, ctorParams, nullptr, args, e->typeArgs, nullptr, l, targs)) {
      return T.unknown();
    }
    ClassSym* cls = instantiateClass(g, targs, l);
    if (!cls) return T.unknown();
    if (cls->isAbstract) return errT(l, "cannot create an instance of an abstract class ('" + cls->name + "')");
    resolveSig(cls->ctor);
    checkArgs(e, cls->ctor->type, args, 0, cls->ctor);
    e->cls = cls;
    e->callee = cls->ctor;
    return cls->type;
  }
  if (s && s->k == SymK::Class) {
    ClassSym* cls = static_cast<ClassSym*>(s);
    resolveClass(cls);
    if (!e->typeArgs.empty()) return errT(l, "class '" + name + "' is not generic");
    if (cls->isAbstract) return errT(l, "cannot create an instance of an abstract class ('" + name + "')");
    resolveSig(cls->ctor);
    checkArgs(e, cls->ctor->type, args, 0, cls->ctor);
    e->cls = cls;
    e->callee = cls->ctor;
    return cls->type;
  }
  if (s && s->k == SymK::Alias && static_cast<AliasSym*>(s)->typeOnlyImport)
    return errT(l, "'" + name + "' cannot be used as a value because it was imported using 'import type'");
  if (s && s->k != SymK::Namespace) return errT(l, "'" + name + "' is not a class");
  auto typeArg = [&](size_t i) -> Type* { return i < e->typeArgs.size() ? resolveType(e->typeArgs[i]) : nullptr; };
  if (name == "Map") {
    Type *kt = typeArg(0), *vt = typeArg(1);
    if (!e->typeArgs.empty() && e->typeArgs.size() != 2) return errT(l, "Map requires two type arguments");
    if (!kt && exp && exp->t == TT::Map) { kt = exp->key; vt = exp->val; }
    if (!args.empty()) {
      if (args.size() != 1) return errT(l, "new Map() takes at most one argument");
      // checked once (checking an expression again is not idempotent), with the entries array as the
      // contextual type; the result may also be a Map (copy constructor)
      Type* want = kt ? T.array(T.tuple({kt, vt})) : nullptr;
      Expr* lit = strip(args[0]);
      if (!kt && lit->k == EK::Array && !lit->list.empty()) {
        // new Map([["a", 1], ["b", 2]]): the key and value types of the entry literals
        bool pairs = true;
        for (Expr* x : lit->list) {
          Expr* p2 = strip(x);
          pairs = pairs && x->k != EK::Spread && p2->k == EK::Array && p2->list.size() == 2 &&
                  p2->list[0]->k != EK::Spread && p2->list[1]->k != EK::Spread;
        }
        if (pairs) {
          Type *k0 = nullptr, *v0 = nullptr;
          for (Expr* x : lit->list) {
            Expr* p2 = strip(x);
            Type* a = widen(check(p2->list[0], nullptr));
            p2->list[0] = wrapConv(p2->list[0], ConvKind::None, a);  // (checked already: not checked again)
            k0 = k0 ? unify(k0, a, p2->list[0]->loc, "Map keys") : a;
            Expr* vx = strip(p2->list[1]);
            if (vx->k == EK::Array && vx->list.empty()) continue;  // [] takes the other values' type
            Type* b = widen(check(p2->list[1], nullptr));
            p2->list[1] = wrapConv(p2->list[1], ConvKind::None, b);
            v0 = v0 ? unify(v0, b, p2->list[1]->loc, "Map values") : b;
          }
          if (k0 && v0 && k0->t != TT::Unknown && v0->t != TT::Unknown && k0->t != TT::Null && k0->t != TT::Undefined &&
              v0->t != TT::Null && v0->t != TT::Undefined) {
            kt = k0;
            vt = v0;
            want = T.array(T.tuple({kt, vt}));
          }
        }
      }
      Type* at = check(args[0], want);
      iterUse(args[0]);
      if (at->t == TT::Map) {
        if (kt && (at->key != kt || at->val != vt)) return errT(args[0]->loc, "Map type mismatch in copy constructor");
        e->intrinsic = IN_NATIVE_NORECV;
        e->native = "Map.copy";
        return at;
      }
      if (!kt) {
        if (at->t == TT::Array && at->elem->t == TT::Tuple && at->elem->elems.size() == 2) {
          kt = at->elem->elems[0];
          vt = at->elem->elems[1];
        } else if (at->t == TT::Array && at->elem->t == TT::Array) {
          return errT(args[0]->loc, "cannot infer Map types from entries; add type arguments: new Map<K, V>(...)");
        }
      }
      if (!kt) return errT(l, "cannot infer Map key/value types; write new Map<K, V>(...)");
      coerce(args[0], T.array(T.tuple({kt, vt})));
      e->cls = tupleRecord(T.tuple({kt, vt}));
    }
    if (!kt) return errT(l, "cannot infer Map key/value types; write new Map<K, V>() or annotate the variable");
    if (kt->t == TT::Optional || kt->t == TT::Void) return errT(l, "invalid Map key type '" + typeName(kt) + "'");
    e->intrinsic = IN_NATIVE_NORECV;
    e->native = args.empty() ? "Map.new" : "Map.fromEntries";
    return T.map(kt, vt);
  }
  if (name == "Set") {
    Type* et = typeArg(0);
    if (!et && exp && exp->t == TT::Set) et = exp->elem;
    if (!args.empty()) {
      if (args.size() != 1) return errT(l, "new Set() takes at most one argument");
      // checked once (checking an expression again is not idempotent), with the array as the contextual
      // type; the result may also be a Set (copy constructor)
      Type* at = check(args[0], et ? T.array(et) : nullptr);
      iterUse(args[0]);
      if (at->t == TT::Set) {
        if (et && at->elem != et) return errT(args[0]->loc, "Set type mismatch in copy constructor");
        e->intrinsic = IN_NATIVE_NORECV;
        e->native = "Map.copy";
        return at;
      }
      if (!et) {
        if (at->t != TT::Array) return errT(args[0]->loc, "new Set(x) expects an array");
        et = at->elem;
      }
      coerce(args[0], T.array(et));
    }
    if (!et) return errT(l, "cannot infer Set element type; write new Set<T>()");
    e->intrinsic = IN_NATIVE_NORECV;
    e->native = args.empty() ? "Set.new" : "Set.fromArray";
    return T.set(et);
  }
  if (name == "Array") {
    Type* et = typeArg(0);
    if (!et && exp && exp->t == TT::Array) et = exp->elem;
    // new Array(a, b, ...) and new Array(x) of a non-number x: an array of these elements
    bool elems = args.size() > 1 || (args.size() == 1 && args[0]->k == EK::Spread);
    if (args.size() == 1 && !elems) {
      Type* at = check(args[0], et && et != num ? et : num);
      elems = at->t != TT::Number && at->t != TT::Int && at->t != TT::Unknown;
      if (elems) args[0] = wrapConv(args[0], ConvKind::None, at);  // (checked already: not checked again)
    }
    if (elems) {
      e->k = EK::Array;
      e->a = nullptr;
      e->typeArgs.clear();
      return check(e, et ? T.array(et) : expected);
    }
    if (!et) return errT(l, "cannot infer Array element type; write new Array<T>(n)");
    if (args.size() == 1) {
      check(args[0], num);
      if (!coerce(args[0], num)) return T.unknown();
    }
    e->intrinsic = IN_NEW_ARRAY;
    return T.array(et);
  }
  if (name == "Uint8Array") {
    if (args.empty()) { args.push_back(mkNum(P, T, 0, l)); nat("Bytes.new"); return T.bytes(); }
    Type* t = check(args[0], nullptr);
    if (t->t == TT::Number || (t->t == TT::Int)) {
      if (args.size() != 1) return errT(l, "new Uint8Array(length) takes one argument");
      coerce(args[0], num);
      nat("Bytes.new");
      return T.bytes();
    }
    if (t->t == TT::Array) {
      if (t->elem != num) return errT(args[0]->loc, "new Uint8Array(array) requires number[]");
      if (args.size() != 1) return errT(l, "too many arguments");
      nat("Bytes.fromArray");
      return T.bytes();
    }
    if (t->t == TT::Bytes) {
      if (args.size() != 1) return errT(l, "too many arguments");
      nat("Bytes.copy");
      return T.bytes();
    }
    if (t->t == TT::ArrayBuffer) {
      if (args.size() == 1) args.push_back(mkNum(P, T, 0, l));
      if (args.size() == 2) args.push_back(mkNum(P, T, NAN, l));
      if (!argsMatch(e, args, {T.arrayBuffer(), num, num}, 3, "Uint8Array")) return T.unknown();
      nat("Bytes.view");
      return T.bytes();
    }
    return errT(args[0]->loc, "unsupported Uint8Array constructor argument '" + typeName(t) + "'");
  }
  if (name == "ArrayBuffer") {
    if (!argsMatch(e, args, {num}, 1, "ArrayBuffer")) return T.unknown();
    nat("Buffer.new");
    return T.arrayBuffer();
  }
  if (name == "DataView") {
    if (args.empty()) return errT(l, "DataView requires a buffer");
    if (args.size() == 1) args.push_back(mkNum(P, T, 0, l));
    if (args.size() == 2) args.push_back(mkNum(P, T, NAN, l));
    if (!argsMatch(e, args, {T.arrayBuffer(), num, num}, 3, "DataView")) return T.unknown();
    nat("Bytes.view");
    return T.dataView();
  }
  if (name == "TextEncoder") {
    if (!args.empty()) return errT(l, "TextEncoder takes no arguments");
    e->intrinsic = IN_CONST_NUM;  // placeholder object (null)
    return T.textEncoder();
  }
  if (name == "TextDecoder") {
    if (args.size() > 1) return errT(l, "TextDecoder takes at most one argument");
    if (args.size() == 1) {
      Expr* a = strip(args[0]);
      if (a->k != EK::Str || (a->str != u"utf-8" && a->str != u"utf8" && a->str != u"UTF-8"))
        return errT(l, "only 'utf-8' TextDecoder is supported");
      check(args[0], T.string());
    }
    e->intrinsic = IN_CONST_NUM;
    return T.textDecoder();
  }
  if (name == "AggregateError") {
    // new AggregateError(errors, message?)
    if (args.empty() || args.size() > 2) return errT(l, "AggregateError takes an array of errors and a message");
    if (args.size() == 1) { Expr* sx = P.arena.expr(EK::Str, l); sx->type = T.string(); args.push_back(sx); }
    if (!argsMatch(e, args, {T.array(T.error()), T.string()}, 2, "AggregateError")) return T.unknown();
    nat("Error.newAggregate");
    return T.error();
  }
  if (name == "Error" || name == "TypeError" || name == "RangeError" || name == "SyntaxError" || name == "URIError" ||
      name == "ReferenceError" || name == "EvalError") {
    Expr* cause = nullptr;
    if (args.size() == 2) {
      // new Error(message, { cause })
      Expr* o = strip(args[1]);
      if (o->k != EK::Object || o->props.size() != 1 || o->props[0].key != "cause" || o->props[0].spread || o->props[0].accessor)
        return errT(args[1]->loc, name + " options must be an object literal { cause: error }");
      cause = o->props[0].value;
      Type* ct = check(cause, T.error());
      if (ct->t != TT::Error && ct->t != TT::Caught && ct->t != TT::Unknown)
        return errT(cause->loc, "the cause of an Error must be an Error (or a caught value), not '" + typeName(ct) + "'");
      args.pop_back();
    } else if (args.size() > 2) {
      return errT(l, name + " takes a message and options { cause }");
    }
    if (args.size() == 1) { check(args[0], T.string()); coerce(args[0], T.string()); }
    if (args.empty()) { Expr* sx = P.arena.expr(EK::Str, l); sx->type = T.string(); args.push_back(sx); }
    if (cause) {
      Expr* nx = P.arena.expr(EK::Str, l);
      nx->type = T.string();
      nx->str = utf8ToUtf16(name);
      args.insert(args.begin(), nx);
      args.push_back(cause);
      nat("Error.newCause");
      return T.error();
    }
    Expr* nx = P.arena.expr(EK::Str, l);
    nx->type = T.string();
    nx->str = utf8ToUtf16(name);
    args.insert(args.begin(), nx);
    nat("Error.new");
    return T.error();
  }
  if (name == "Date") {
    if (args.empty()) { nat("Date.newNow"); return T.date(); }
    if (args.size() == 1) {
      Type* t = check(args[0], nullptr);
      if (t->t == TT::String) { nat("Date.fromString"); return T.date(); }
      if (t->t == TT::Date) { nat("Date.copy"); return T.date(); }
      if (!coerce(args[0], num)) return T.unknown();
      nat("Date.fromNumber");
      return T.date();
    }
    if (args.size() > 7) return errT(l, "Date takes at most 7 arguments");
    while (args.size() < 7) args.push_back(mkNum(P, T, args.size() == 2 ? 1 : 0, l));
    if (!argsMatch(e, args, {num, num, num, num, num, num, num}, 7, "Date")) return T.unknown();
    nat("Date.fromParts");
    return T.date();
  }
  if (int tk = typedArrayKind(name)) {
    // new Int32Array(length | array | typed array | buffer [, byteOffset [, length]])
    Type* tat = T.typedArray((uint8_t)tk);
    Type* et = taElem(tat);
    if (args.empty()) args.push_back(mkNum(P, T, 0, l));
    if (args.size() == 1) {
      Type* t = check(args[0], nullptr);
      if (t->t == TT::Number || t->t == TT::Int) {
        if (!coerce(args[0], num)) return T.unknown();
        nat("TA.new");
      } else if (t->t == TT::ArrayBuffer) {
        args.push_back(mkNum(P, T, 0, l));
        args.push_back(mkNum(P, T, -1, l));
        nat("TA.fromBuffer");
      } else if (t->t == TT::Array || t->t == TT::TypedArray || t->t == TT::Bytes) {
        if (t->t == TT::Array && baseLit(t->elem) != et) return errT(args[0]->loc, name + " needs an array of " + typeName(et) + "s, not '" + typeName(t) + "'");
        nat("TA.from");
      } else {
        return errT(args[0]->loc, "cannot make a " + name + " from '" + typeName(t) + "'");
      }
    } else {
      // (buffer, byteOffset, length?)
      if (args.size() == 2) args.push_back(mkNum(P, T, -1, l));
      if (!argsMatch(e, args, {T.arrayBuffer(), num, num}, 3, name.c_str())) return T.unknown();
      nat("TA.fromBuffer");
    }
    args.insert(args.begin(), intLit(tk, l));
    return tat;
  }
  if (name == "Promise") {
    // new Promise<T>((resolve, reject) => { ... })
    Type* vt = nullptr;
    if (!e->typeArgs.empty()) vt = resolveType(e->typeArgs[0]);
    else if (exp && exp->t == TT::Promise) vt = exp->elem;
    if (!vt) return errT(l, "write the promise's value type: new Promise<T>((resolve, reject) => ...)");
    if (vt->t == TT::Undefined) vt = T.voidT();
    if (args.size() != 1) return errT(l, "new Promise takes one executor function");
    std::vector<Type*> rp;
    if (vt->t != TT::Void) rp.push_back(vt);
    Type* resolveT = T.func(rp, T.voidT());
    Type* rejectT = T.func({T.error()}, T.voidT());
    Type* ft = check(args[0], T.func({resolveT, rejectT}, T.voidT()));
    if (ft->t != TT::Func) return errT(args[0]->loc, "new Promise expects an executor function");
    coerce(args[0], T.func({resolveT, rejectT}, T.voidT()), "executor");
    e->intrinsic = IN_NEW_PROMISE;
    return T.promise(vt);
  }
  if (name == "RegExp") return errT(l, kNoRegExp);
  if (name == "WeakMap" || name == "WeakSet" || name == "Proxy")
    return errT(l, "'" + name + "' is not supported in E7");
  return errT(l, "cannot find class '" + name + "'");
}

}  // namespace e7
