// Checker: expressions.
#include <cmath>

#include "../core/numfmt.h"
#include "../regexp/regexp.h"
#include "checker.h"
#include "intrinsics.h"
#include "suggest.h"

namespace e7 {

static Expr* strip(Expr* e) {
  while (e && e->k == EK::Paren) e = e->a;
  return e;
}

Type* Checker::check(Expr* e, Type* expected) {
  Type* t = checkInner(e, expected);
  if (!t) t = T.unknown();
  e->type = t;
  return t;
}

Type* Checker::checkCond(Expr* e) {
  Type* t = check(e, T.boolean());
  if (t->t == TT::Void) err(e->loc, "an expression of type 'void' cannot be tested for truthiness");
  return t;
}

Type* Checker::checkInner(Expr* e, Type* expected) {
  switch (e->k) {
    case EK::Num: return checkNum(e, expected);
    case EK::BigLit: return T.bigint();
    case EK::Str: return T.string();
    case EK::Template: return checkTemplate(e);
    case EK::Bool: return T.boolean();
    case EK::Null: return T.nullT();
    case EK::Undefined: return T.undef();
    case EK::Ident: return checkIdent(e, expected);
    case EK::This: {
      Sym* s = lookup("this");
      if (!s || s->k != SymK::Var) return errT(e->loc, "'this' is only allowed in methods and constructors");
      VarSym* v = static_cast<VarSym*>(s);
      e->var = v;
      markCapture(v);
      return v->type;
    }
    case EK::Super: return errT(e->loc, "'super' can only be used as super(...) or super.method(...)");
    case EK::Array: return checkArray(e, expected);
    case EK::Object: return checkObject(e, expected);
    case EK::Unary: return checkUnary(e, expected);
    case EK::Update: return checkUpdate(e);
    case EK::Binary: return checkBinary(e, expected);
    case EK::Logical: return checkLogical(e, expected);
    case EK::Assign: return checkAssign(e);
    case EK::Cond: return checkCondExpr(e, expected);
    case EK::Call: return checkCall(e, expected);
    case EK::New: return checkNew(e, expected);
    case EK::Member: return checkMember(e, expected);
    case EK::Index: return checkIndex(e);
    case EK::NonNull: {
      Expr* inner = strip(e->a);
      Type* t = check(e->a, expected ? T.optional(expected, true, true) : nullptr);
      (void)inner;
      if (t->t == TT::Optional) return t->elem;
      if (t->t == TT::Null || t->t == TT::Undefined) return errT(e->loc, "value is always null/undefined");
      return t;
    }
    case EK::As: return checkAs(e);
    case EK::Arrow: return checkArrow(e, expected);
    case EK::Spread: return errT(e->loc, "spread is not allowed here");
    case EK::Yield: return checkYield(e);
    case EK::Await: return checkAwait(e);
    case EK::RegExp: return checkRegExpLit(e);
    case EK::Paren: return check(e->a, expected);
    case EK::Conv: return e->type;
    case EK::TypeOf: {
      Type* t = check(e->a, nullptr);
      std::u16string s;
      switch (t->t) {
        case TT::Number: case TT::Int: s = u"number"; break;
        case TT::String: s = u"string"; break;
        case TT::Bool: s = u"boolean"; break;
        case TT::BigInt: s = u"bigint"; break;
        case TT::Func: s = u"function"; break;
        case TT::Undefined: s = u"undefined"; break;
        case TT::Enum: s = t->en->isString ? u"string" : u"number"; break;
        case TT::Optional: return errT(e->loc, "typeof on nullable values is not supported; compare with null/undefined");
        default: s = u"object"; break;
      }
      e->str = s;
      e->intrinsic = IN_TYPEOF_CONST;
      return T.string();
    }
  }
  return T.unknown();
}

// ------------------------------------------------------------------ literals
// A regular expression literal (D4): checked with the engine (a syntax error is a compile error with
// V8's message) and stored in the module's table; its type knows the groups
Type* Checker::checkRegExpLit(Expr* e) {
  std::string err;
  re::ProgramPtr p = re::compile(utf8ToUtf16(e->raw), e->regexFlags, err);
  if (!p) {
    if (err.rfind("Invalid flags", 0) == 0) return errT(e->loc, "Invalid regular expression flags");
    return errT(e->loc, err);
  }
  int idx = -1;
  for (size_t i = 0; i < P.regexps.size() && idx < 0; i++)
    if (P.regexps[i].first == e->raw && P.regexps[i].second == e->regexFlags) idx = (int)i;
  if (idx < 0) {
    idx = (int)P.regexps.size();
    P.regexps.push_back({e->raw, e->regexFlags});
  }
  e->ival = idx;
  std::vector<std::string> names;
  for (auto& g : re::namedGroups(*p)) names.push_back(g.name);
  return T.regexp((int32_t)re::captureCount(*p), names);
}

Type* Checker::regexpGroupsType(Type* re) {
  std::vector<ObjShape::Prop> props;
  for (auto& n : re->reNames) props.push_back({n, T.optional(T.string(), false, true), false, true, false});
  return T.anonObject(props);
}

static bool parseUnsigned(const std::string& raw, uint64_t& out) {
  // raw is decimal digits or 0x / 0o / 0b prefixed digits (no separators)
  int radix = 10;
  size_t i = 0;
  if (raw.size() > 1 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'o' || raw[1] == 'b')) {
    radix = raw[1] == 'x' ? 16 : raw[1] == 'o' ? 8 : 2;
    i = 2;
  }
  uint64_t v = 0;
  for (; i < raw.size(); i++) {
    char c = raw[i];
    int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'z') ? c - 'a' + 10 : (c >= 'A' && c <= 'Z') ? c - 'A' + 10 : 99;
    if (d >= radix) return false;
    if (v > (UINT64_MAX - d) / radix) return false;
    v = v * radix + d;
  }
  out = v;
  return true;
}

bool Checker::litFits(Expr* lit, Type* it, int64_t& out, bool neg, std::string& why) {
  uint64_t mag;
  if (!lit->isIntLit || !parseUnsigned(lit->raw, mag)) {
    why = "literal is not an integer in range";
    return false;
  }
  bool sign = isSignedInt(it);
  int bits = intBits(it);
  if (neg) {
    if (!sign) { why = "negative value for unsigned type '" + typeName(it) + "'"; return false; }
    uint64_t lim = bits == 64 ? (1ull << 63) : (1ull << (bits - 1));
    if (mag > lim) { why = "value out of range for '" + typeName(it) + "'"; return false; }
    out = (int64_t)(0 - mag);
    return true;
  }
  uint64_t lim = sign ? (bits == 64 ? (uint64_t)INT64_MAX : (1ull << (bits - 1)) - 1)
                      : (bits == 64 ? UINT64_MAX : (1ull << bits) - 1);
  if (mag > lim) { why = "value " + lit->raw + " out of range for '" + typeName(it) + "'"; return false; }
  out = (int64_t)mag;
  return true;
}

bool Checker::isIntLiteralExpr(Expr* e) {
  e = strip(e);
  if (e->k == EK::Num) return e->isIntLit;
  if (e->k == EK::Unary && e->raw == "-") {
    Expr* x = strip(e->a);
    return x->k == EK::Num && x->isIntLit;
  }
  return false;
}

Type* Checker::checkNum(Expr* e, Type* expected) {
  Type* exp = expected ? baseOf(expected) : nullptr;
  if (exp && exp->t == TT::Int) {
    std::string why;
    int64_t v;
    if (!e->isIntLit) return errT(e->loc, "a fractional number literal cannot be used as '" + typeName(exp) + "'");
    if (!litFits(e, exp, v, false, why)) return errT(e->loc, why);
    e->ival = v;
    return exp;
  }
  if (exp && exp->t == TT::BigInt && e->isIntLit) return errT(e->loc, "number literal is not a bigint; write " + e->raw + "n");
  return T.number();
}

Type* Checker::checkTemplate(Expr* e) {
  for (Expr*& x : e->list) {
    Type* t = check(x, nullptr);
    if (t->t == TT::Void) err(x->loc, "void value in template literal");
  }
  return T.string();
}

// ------------------------------------------------------------------ identifiers
Type* Checker::checkIdent(Expr* e, Type* expected) {
  Sym* s = lookup(e->raw);
  if (!s && e->a && e->raw.find('.') != std::string::npos) {
    // `ns.C` rewritten by resolveNamespaceRefs, but here `ns` is not the module namespace (a local shadows
    // it): a member access again
    e->k = EK::Member;
    e->raw = e->raw.substr(e->raw.rfind('.') + 1);
    return checkMember(e, expected);
  }
  if (!s) return errT(e->loc, "cannot find name '" + e->raw + "'" + suggestName(e->raw, false));
  switch (s->k) {
    case SymK::Var: {
      VarSym* v = static_cast<VarSym*>(s);
      e->var = v;
      if (v->isGlobal && v->globalIdx < 0) {
        e->intrinsic = IN_CONST_NUM;
        e->num = v->globalIdx == -2 ? NAN : INFINITY;
        return T.number();
      }
      Type* declared = v->isGlobal ? globalVarType(v) : v->type;
      if (!declared) return errT(e->loc, "'" + e->raw + "' is used before its declaration");
      markCapture(v);
      Type* n = narrowed(varKey(v), v->isConst);
      return n ? n : declared;
    }
    case SymK::Func: {
      FuncSym* f = static_cast<FuncSym*>(s);
      if (f->isNative) return errT(e->loc, "native function '" + f->name + "' cannot be used as a value");
      resolveSig(f);
      if (f->inferRet) {
        if (f->checking) return errT(e->loc, "'" + f->name + "' is referenced recursively; add an explicit return type");
        checkFunction(f);
      }
      if (f->hasDefaults) {
        // a function with default values as a value: an arrow function that calls it
        if (Type* t = etaExpand(e, expected, (int)f->type->params.size())) return t;
        return errT(e->loc, "functions with default parameters can be used as values only where a function type is expected; wrap it in an arrow function");
      }
      e->callee = f;
      if (f->selfVar) markCapture(f->selfVar);
      return f->type;
    }
    case SymK::Generic: {
      GenericSym* g = static_cast<GenericSym*>(s);
      if (g->kind == GenericSym::Class) return errT(e->loc, "class '" + e->raw + "' cannot be used as a value");
      if (g->kind != GenericSym::Func && g->kind != GenericSym::Arrow)
        return errT(e->loc, "'" + e->raw + "' is a type, not a value");
      FuncSym* f = genericFuncValue(g, e, expected);
      if (!f) return T.unknown();
      if (f->inferRet && !f->checking) checkFunction(f);
      if (f->hasDefaults) return errT(e->loc, "functions with default parameters cannot be used as values; wrap it in an arrow function");
      e->callee = f;
      if (f->selfVar) markCapture(f->selfVar);
      return f->type;
    }
    case SymK::Class: return errT(e->loc, "class '" + e->raw + "' cannot be used as a value");
    case SymK::Enum: return errT(e->loc, "enum '" + e->raw + "' cannot be used as a value");
    case SymK::Namespace:
      // String / Number / Boolean / BigInt as conversion functions: arr.map(String), arr.filter(Boolean)
      if (static_cast<NamespaceSym*>(s)->builtin && (e->raw == "String" || e->raw == "Number" || e->raw == "Boolean" || e->raw == "BigInt"))
        if (Type* t = etaExpand(e, expected, 1)) return t;
      return errT(e->loc, "'" + e->raw + "' cannot be used as a value");
    case SymK::Builtin:
      if (Type* t = etaExpand(e, expected, builtinArity(static_cast<BuiltinSym*>(s)->id))) return t;
      return errT(e->loc, "builtin function '" + e->raw + "' can be used as a value only where a function type is expected; wrap it in an arrow function");
    default:
      if (s->k == SymK::Alias && static_cast<AliasSym*>(s)->typeOnlyImport)
        return errT(e->loc, "'" + e->raw + "' cannot be used as a value because it was imported using 'import type'");
      return errT(e->loc, "'" + e->raw + "' is a type, not a value");
  }
}

// ------------------------------------------------------------------ records
Type* Checker::recordFor(const std::vector<std::pair<std::string, Type*>>& fields, bool tuple,
                         const std::vector<std::pair<std::string, Type*>>* methods, bool accessors) {
  size_t nm = methods ? methods->size() : 0;
  // find identical layout (names + value types, and the same methods)
  for (ClassSym* c : recordClasses) {
    if (c->fields.size() != fields.size() || c->isTuple != tuple || c->vtable.size() != nm || c->hasAccessors != accessors) continue;
    if (accessors) {
      // the accessor pairs (field types) decide the getters' and setters' types
      bool samePairs = true;
      for (size_t i = 0; i < fields.size() && samePairs; i++)
        samePairs = c->fields[i].name == fields[i].first && c->fields[i].type == fields[i].second;
      if (!samePairs) continue;
    }
    bool same = true;
    for (size_t i = 0; i < fields.size() && same; i++)
      if (c->fields[i].name != fields[i].first || vtOf(c->fields[i].type) != vtOf(fields[i].second)) same = false;
    for (size_t i = 0; i < nm && same; i++)
      if (c->vtable[i].name != (*methods)[i].first || c->vtable[i].fn->type != (*methods)[i].second) same = false;
    if (same) {
      if (!tuple && P.presenceMasks)
        for (auto& f : fields) c->presence = c->presence || (f.second->t == TT::Optional && f.second->hasUndef) || f.second->t == TT::Undefined;
      return c->type;
    }
  }
  ClassSym* c = P.make<ClassSym>();
  c->name = "{";
  for (size_t i = 0; i < fields.size(); i++) c->name += (i ? "," : "") + fields[i].first;
  c->name += "}";
  c->isRecord = true;
  c->isTuple = tuple;
  c->hasAccessors = accessors;
  c->state = 2;
  for (auto& f : fields) {
    c->fieldIdx[f.first] = (int)c->fields.size();
    c->fields.push_back({f.first, f.second, nullptr, false, c, Loc()});
    if (!tuple && P.presenceMasks) c->presence = c->presence || (f.second->t == TT::Optional && f.second->hasUndef) || f.second->t == TT::Undefined;
  }
  c->type = T.classType(c);
  // methods of an object literal (method shorthand, or function properties implementing an
  // interface's methods): vtable entries that call the closure stored in the field of that name,
  // so that calls through interface types (by name) reach them; accessors "get x" / "set x" call the
  // getter / setter of the accessor pair in field x
  for (size_t i = 0; i < nm; i++) {
    const std::string& mn = (*methods)[i].first;
    FuncSym* t = P.make<FuncSym>();
    t->name = mn;
    t->qualName = mn;
    t->cls = c;
    t->isMethod = true;
    t->isTopLevel = true;
    if (accessors && (mn.compare(0, 4, "get ") == 0 || mn.compare(0, 4, "set ") == 0) && c->fieldIdx.count(mn.substr(4))) {
      t->accessor = mn[0] == 'g' ? 1 : 2;
      t->trampField = c->fieldIdx[mn.substr(4)];
    } else {
      t->trampField = c->fieldIdx[mn];
    }
    t->type = (*methods)[i].second;
    t->sigResolved = t->checked = true;
    t->file = curFile;
    t->thisVar = newVar("this", c->type, true, Loc());
    t->thisVar->isThis = true;
    t->thisVar->owner = t;
    P.funcs.push_back(t);
    c->methodIdx[mn] = (int)c->vtable.size();
    c->vtable.push_back({mn, t});
  }
  c->classIdx = (int)P.classes.size();
  P.classes.push_back(c);
  recordClasses.push_back(c);
  return c->type;
}

Type* Checker::accessorPairType(Type* t) {
  return T.tuple({T.func({}, t), T.func({t}, T.voidT()), t});
}

bool Checker::readableProps(Type* t, Loc l, const char* what, std::vector<std::pair<std::string, Type*>>& out) {
  Type* b = baseOf(t);
  if (b->t == TT::Object && b->shape->ownAccessors)
    for (auto& n : b->shape->order)
      if (!b->shape->findProp(n) && !b->shape->findMethod("get " + n)) {
        err(l, std::string(what) + " is not supported for an object with the setter-only accessor '" + n +
                   "' (its value would be undefined); add a getter");
        return false;
      }
  out = enumerableProps(b);
  return true;
}

ClassSym* Checker::tupleRecord(Type* tt) {
  std::vector<std::pair<std::string, Type*>> fs;
  for (size_t i = 0; i < tt->elems.size(); i++) fs.push_back({std::to_string(i), tt->elems[i]});
  return recordFor(fs, true)->cls;
}

// ------------------------------------------------------------------ arrays & objects
Type* Checker::elemTypeForIter(Expr* e, Type* t, Loc l) {
  (void)e;
  switch (t->t) {
    case TT::Array: return t->elem;
    case TT::Set: return t->elem;
    case TT::String: return T.string();
    case TT::Bytes: return T.number();
    case TT::Map: return T.tuple({t->key, t->val});
    case TT::Generator: return t->elem;
    case TT::TypedArray: return taElem(t);
    case TT::Class:
      if (Type* g = iteratorMethodType(t, l)) return g->elem;
      return errT(l, "type '" + typeName(t) + "' is not iterable");
    case TT::Unknown: return T.unknown();
    default: return errT(l, "type '" + typeName(t) + "' is not iterable");
  }
}

// The iterator a class instance's [Symbol.iterator]() method returns (a Generator type), or null.
Type* Checker::iteratorMethodType(Type* t, Loc l) {
  if (t->t != TT::Class) return nullptr;
  ClassSym* c = t->cls;
  resolveClass(c);
  auto it = c->methodIdx.find("[Symbol.iterator]");
  if (it == c->methodIdx.end()) return nullptr;
  FuncSym* f = c->vtable[it->second].fn;
  resolveSig(f);
  if (f->inferRet) checkFunction(f);
  Type* r = f->type->ret;
  if (r->t != TT::Generator || !f->type->params.empty()) {
    err(l, "the [Symbol.iterator]() method of '" + c->name + "' must take no arguments and return a Generator, Iterator or IterableIterator");
    return nullptr;
  }
  return r;
}

// await p: the value of a Promise<T> (another value is awaited as Promise.resolve(value))
Type* Checker::checkAwait(Expr* e) {
  FuncSym* f = curFn;
  if (!f || (!f->isAsync && !f->isInit)) return errT(e->loc, "'await' is only allowed in async functions and at the top level of the entry module");
  if (f->isInit) {
    if (P.files.empty() || curFile != P.files.back()->path)
      return errT(e->loc, "top-level 'await' is only supported in the entry module (this module is imported)");
    if (!f->isAsync) {
      f->isAsync = true;
      f->type = T.func({}, T.promise(T.voidT()));
    }
  }
  Type* t = check(e->a, nullptr);
  if (t->t == TT::Promise) return t->elem;
  if (t->t == TT::Void) return errT(e->a->loc, "cannot await a void value");
  return t;
}

Expr* Checker::intLit(int64_t v, Loc l) {
  Expr* x = P.arena.expr(EK::Num, l);
  x->num = (double)v;
  x->isIntLit = true;
  x->ival = v;
  x->type = T.intT(VT_I64);
  return x;
}

// The number of arguments a builtin function takes (-1: any number)
int Checker::builtinArity(const std::string& id) {
  static const char* const one[] = {
      "parseFloat", "isNaN", "isFinite", "encodeURIComponent", "decodeURIComponent", "encodeURI", "decodeURI",
      "Math.abs", "Math.acos", "Math.acosh", "Math.asin", "Math.asinh", "Math.atan", "Math.atanh", "Math.cbrt",
      "Math.ceil", "Math.clz32", "Math.cos", "Math.cosh", "Math.exp", "Math.expm1", "Math.floor", "Math.fround",
      "Math.log", "Math.log10", "Math.log1p", "Math.log2", "Math.round", "Math.sign", "Math.sin", "Math.sinh",
      "Math.sqrt", "Math.tan", "Math.tanh", "Math.trunc", "Number.isInteger", "Number.isFinite", "Number.isNaN",
      "Number.isSafeInteger", "Number.parseFloat", "Array.isArray", "i8", "u8", "i16", "u16", "i32", "u32", "i64",
      "u64", "int", "clz32", "clz64", "ctz32", "ctz64", "popcnt", "bswap16", "bswap32", "bswap64"};
  static const char* const two[] = {"parseInt", "Number.parseInt", "Math.atan2", "Math.pow", "Math.imul", "rotl32",
                                    "rotr32", "rotl64", "rotr64", "mulhi64", "imulhi64", "idiv", "imod"};
  for (const char* n : one)
    if (id == n) return 1;
  for (const char* n : two)
    if (id == n) return 2;
  return -1;
}

bool Checker::isNamespaceConstant(const std::string& ns, const std::string& name) {
  if (ns == "Math") return name == "PI" || name == "E" || name == "LN2" || name == "LN10" || name == "LOG2E" || name == "LOG10E" || name == "SQRT2" || name == "SQRT1_2";
  if (ns == "Number") return name == "MAX_SAFE_INTEGER" || name == "MIN_SAFE_INTEGER" || name == "EPSILON" || name == "MAX_VALUE" || name == "MIN_VALUE" || name == "NaN" || name == "POSITIVE_INFINITY" || name == "NEGATIVE_INFINITY";
  return false;
}

// A function used as a value where a function type is expected, when it is not a closure itself (a
// builtin, a function with default values): rewritten to the arrow function (a, b) => f(a, b) with
// the expected function's parameters (at most `arity` of them; -1: all), then checked as one
Type* Checker::etaExpand(Expr* e, Type* expected, int arity) {
  Type* ft = expected ? baseOf(expected) : nullptr;
  if (!ft || ft->t != TT::Func) return nullptr;
  size_t n = ft->params.size();
  if (arity >= 0 && (size_t)arity < n) n = (size_t)arity;
  Expr* callee = P.arena.expr(e->k, e->loc);
  *callee = *e;
  Expr* call = P.arena.expr(EK::Call, e->loc);
  call->a = callee;
  std::vector<Param> ps;
  for (size_t i = 0; i < n; i++) {
    Param p;
    p.name = "$arg" + std::to_string(i);
    p.loc = e->loc;
    ps.push_back(p);
    Expr* id = P.arena.expr(EK::Ident, e->loc);
    id->raw = p.name;
    call->list.push_back(id);
  }
  Loc l = e->loc;
  *e = Expr();
  e->k = EK::Arrow;
  e->loc = l;
  e->params = ps;
  e->exprBody = call;
  return checkArrow(e, expected);
}

// yield v / yield* iterable (in the body of a generator)
Type* Checker::checkYield(Expr* e) {
  FuncSym* f = curFn;
  if (!f || !f->isGenerator) return errT(e->loc, "'yield' is only allowed in the body of a generator function (function*)");
  Type* g = f->inferRet ? nullptr : f->type->ret;
  if (g && g->t != TT::Generator) g = nullptr;
  if (e->delegate) {
    Type* it = check(e->a, nullptr);
    Type* et;
    Type* res = T.voidT();
    if (it->t == TT::Generator) {
      et = it->elem;
      res = it->ret && it->ret->t != TT::Void ? it->ret : T.voidT();
      Type* outerNext = g && g->val ? g->val : T.voidT();
      Type* innerNext = it->val ? it->val : T.voidT();
      if (innerNext->t != TT::Void && innerNext != outerNext)
        err(e->loc, "yield* of '" + typeName(it) + "': its next() values must have the type of this generator's ('" +
                        typeName(outerNext) + "')");
    } else if (Type* mt = iteratorMethodType(it, e->a->loc)) {
      et = mt->elem;
    } else {
      et = elemTypeForIter(e->a, it, e->a->loc);
    }
    if (g) {
      if (!assignable(et, g->elem)) err(e->loc, "type '" + typeName(et) + "' is not assignable to the yielded type '" + typeName(g->elem) + "'");
    } else {
      f->yieldTypes.push_back(et);
    }
    e->innerType = et;
    return res;
  }
  if (e->a) {
    Type* t = check(e->a, g ? g->elem : nullptr);
    if (g) coerce(e->a, g->elem, "yield");
    else f->yieldTypes.push_back(t);
  } else {
    if (g && !assignable(T.undef(), g->elem))
      err(e->loc, "'yield' without a value yields undefined, which is not assignable to '" + typeName(g->elem) + "'");
    if (!g) f->yieldTypes.push_back(T.undef());
  }
  Type* next = g && g->val ? g->val : T.voidT();
  return next->t == TT::Void ? T.undef() : next;
}

Type* Checker::checkArray(Expr* e, Type* expected) {
  Type* exp = expected ? baseOf(expected) : nullptr;
  expandSpreads(e->list);  // [...tuple]: one element per tuple element
  if (exp && exp->t == TT::Tuple) {
    if (e->list.size() != exp->elems.size())
      return errT(e->loc, "expected a tuple of " + std::to_string(exp->elems.size()) + " elements");
    for (size_t i = 0; i < e->list.size(); i++) {
      if (e->list[i]->k == EK::Spread) return errT(e->loc, "only tuples can be spread into tuple literals");
      if (!e->list[i]->type) check(e->list[i], exp->elems[i]);
      coerce(e->list[i], exp->elems[i]);
    }
    e->cls = tupleRecord(exp);
    return exp;
  }
  Type* elemExp = exp && exp->t == TT::Array ? exp->elem : nullptr;
  if (e->list.empty()) {
    if (elemExp) return exp;
    return errT(e->loc, "cannot infer the type of an empty array literal; add a type annotation");
  }
  Type* et = elemExp;
  std::vector<Type*> types;
  // [[1], []]: an empty array element takes its type from the other elements
  auto isEmptyArr = [](Expr* x) {
    x = strip(x);
    return x->k == EK::Array && x->list.empty() && !x->type;
  };
  Type* emptyCtx = nullptr;
  if (!elemExp) {
    bool anyEmpty = false;
    for (Expr* x : e->list) anyEmpty |= isEmptyArr(x);
    if (anyEmpty)
      for (Expr* x : e->list)
        if (!isEmptyArr(x) && x->k != EK::Spread) {
          Type* t = x->type ? x->type : check(x, nullptr);
          if (baseOf(t)->t == TT::Array) { emptyCtx = baseOf(t); break; }
        }
  }
  for (Expr*& x : e->list) {
    Type* t;
    if (!elemExp && emptyCtx && isEmptyArr(x)) {
      t = check(x, emptyCtx);
    } else if (x->k == EK::Spread) {
      Type* st = x->type;  // checked by expandSpreads
      if (st->t == TT::Array || st->t == TT::Set) t = st->elem;
      else if (st->t == TT::String) t = T.string();
      else if (st->t == TT::Bytes) t = T.number();
      else if (st->t == TT::Map) t = T.tuple({st->key, st->val});
      else if (st->t == TT::Generator) t = st->elem;
      else if (st->t == TT::TypedArray) t = taElem(st);
      else if (Type* gt = st->t == TT::Class ? iteratorMethodType(st, x->loc) : nullptr) t = gt->elem;
      else { err(x->loc, "type '" + typeName(st) + "' cannot be spread into an array"); t = T.unknown(); }
      if (elemExp && t != elemExp && !(t->t == TT::Unknown) && !(kindOf(t) == K_R && kindOf(elemExp) == K_R && assignable(t, elemExp)))
        err(x->loc, "spread element type '" + typeName(t) + "' is not assignable to '" + typeName(elemExp) + "'");
    } else {
      t = x->type ? x->type : check(x, elemExp);
      if (elemExp) coerce(x, elemExp);
    }
    types.push_back(t);
  }
  if (!et) {
    for (Type* t : types) et = et ? unify(et, t, e->loc, "array literal") : t;
    if (et->t == TT::Null || et->t == TT::Undefined) return errT(e->loc, "cannot infer array element type from null/undefined");
    if (et->t == TT::Void) return errT(e->loc, "array elements cannot be void");
    for (Expr*& x : e->list)
      if (x->k != EK::Spread) coerce(x, et);
  }
  return T.array(et);
}

// The properties of an object literal with spreads, in JavaScript order: entries[i] are the properties
// that e->props[i] supplies (a spread: those of the source's type, in their order); a later entry
// overrides an earlier one and a key keeps its first position.
static void spreadUpsert(const std::vector<ObjProp>& props, const std::vector<std::vector<std::pair<std::string, Type*>>>& entries,
                         std::vector<std::string>& names, std::vector<Type*>& types, std::vector<int>& src,
                         std::vector<std::vector<int>>& cond) {
  auto upsert = [&](const std::string& k, Type* t, int idx) {
    for (size_t i = 0; i < names.size(); i++)
      if (names[i] == k) {
        // an optional spread property only overrides when present (TS semantics)
        Type* bt = baseOf(t);
        if (props[idx].spread && t->t == TT::Optional && t->hasUndef && !t->hasNull && bt == baseOf(types[i])) {
          cond[i].push_back(idx);
          return;
        }
        if (types[i]->t == TT::Optional && types[i]->hasUndef && src[i] >= 0 && props[src[i]].spread) {
          // possibly-absent key from a spread: JS places a newly added key last (best static approximation)
          names.erase(names.begin() + i); types.erase(types.begin() + i);
          src.erase(src.begin() + i); cond.erase(cond.begin() + i);
          break;
        }
        types[i] = t; src[i] = idx; cond[i].clear();
        return;
      }
    names.push_back(k);
    types.push_back(t);
    src.push_back(idx);
    cond.emplace_back();
  };
  for (size_t pi = 0; pi < props.size(); pi++)
    for (auto& en : entries[pi]) upsert(en.first, en.second, (int)pi);
}

// After checking: the spread literals whose first spread source has an object type get, for every
// class or record that such a value can be at run time with its properties in another order, a record
// with the result's properties in the order JavaScript gives them (the source's own key order). The
// new records can be spread sources themselves: repeated until no record is added.
void Checker::spreadOrders() {
  for (int round = 0; round < 8; round++) {
    size_t nclasses = P.classes.size();
    for (auto& sl : spreadLits) {
      Expr* e = sl.e;
      e->spreadAltSrc = -1;
      e->spreadAltTest.clear();
      e->spreadAltCls.clear();
      if (!e->cls || !e->type) continue;
      int si = -1;
      for (size_t pi = 0; pi < e->props.size() && si < 0; pi++) {
        Type* vt = e->props[pi].value->type;
        if (e->props[pi].spread && vt && vt->t == TT::Object && !sl.entries[pi].empty()) si = (int)pi;
      }
      if (si < 0) continue;
      ClassSym* sc = e->cls;
      const auto& ents = sl.entries[si];
      for (size_t ci = 0; ci < P.classes.size(); ci++) {
        ClassSym* k = P.classes[ci];
        if (k->isTuple || k->genericRole == 1 || k->classIdx < 0 || k == sc) continue;
        bool all = true;
        for (auto& en : ents) all = all && k->fieldIdx.count(en.first);
        if (!all) continue;
        auto re = ents;
        std::stable_sort(re.begin(), re.end(), [&](const std::pair<std::string, Type*>& x, const std::pair<std::string, Type*>& y) {
          return k->fieldIdx[x.first] < k->fieldIdx[y.first];
        });
        bool same = true;
        for (size_t i = 0; i < re.size() && same; i++) same = re[i].first == ents[i].first;
        if (same) continue;
        auto entries = sl.entries;
        entries[si] = re;
        std::vector<std::string> names;
        std::vector<Type*> types;
        std::vector<int> src;
        std::vector<std::vector<int>> cond;
        spreadUpsert(e->props, entries, names, types, src, cond);
        // the same fields (types of the static layout), in this order; absent ones last as before
        std::vector<std::pair<std::string, Type*>> fields;
        for (auto& n : names) {
          auto it = sc->fieldIdx.find(n);
          if (it != sc->fieldIdx.end()) fields.push_back({n, sc->fields[it->second].type});
        }
        for (auto& f : sc->fields)
          if (std::find(names.begin(), names.end(), f.name) == names.end()) fields.push_back({f.name, f.type});
        if (fields.size() != sc->fields.size()) continue;
        bool sameOrder = true;
        for (size_t i = 0; i < fields.size() && sameOrder; i++) sameOrder = fields[i].first == sc->fields[i].name;
        if (sameOrder) continue;
        ClassSym* vc = recordFor(fields)->cls;
        e->spreadAltTest.push_back(k);
        e->spreadAltCls.push_back(vc);
      }
      if (!e->spreadAltTest.empty()) e->spreadAltSrc = si;
    }
    if (P.classes.size() == nclasses) break;
  }
}

// The property name a computed key `[K]` stands for: K must be a constant known at compile time (a string
// or number literal, a const of a literal type, an enum member)
bool Checker::constKey(Expr* k, std::string& out) {
  Type* t = check(k, nullptr);
  Expr* x = strip(k);
  if (x->intrinsic == IN_CONST_STR) { out = utf16ToUtf8(x->str); return true; }
  if (x->intrinsic == IN_CONST_NUM) { out = numberToString(x->num); return true; }
  Type* lt = constLit(k);
  if (!lt && t && t->lit) lt = t;
  if (lt && lt->strLits.size() == 1 && lt->numLits.empty()) { out = utf16ToUtf8(lt->strLits[0]); return true; }
  if (lt && lt->numLits.size() == 1 && lt->strLits.empty()) { out = numberToString(lt->numLits[0]); return true; }
  err(k->loc, "a computed property name must be a constant known at compile time (a string or number literal, a const of a literal type, or an enum member)");
  return false;
}

Type* Checker::checkObject(Expr* e, Type* expected) {
  Type* exp = expected ? baseOf(expected) : nullptr;
  for (auto& p : e->props)
    if (p.computed) {
      std::string key;
      if (!constKey(p.computed, key)) return T.unknown();
      p.key = key;
      p.computed = nullptr;
      for (auto& q : e->props)
        if (&q != &p && !q.spread && q.key == key && !q.computed && !(p.accessor && q.accessor && p.accessor != q.accessor))
          return errT(p.loc, "duplicate property '" + key + "'");
    }
  bool anySpread = false, anyMethod = false, anyAccessor = false;
  for (auto& p : e->props) {
    anySpread |= p.spread;
    anyMethod |= p.method;
    anyAccessor |= p.accessor != 0;
    if (p.coverInit) return errT(p.loc, "'=' is only allowed in a destructuring assignment ({ " + p.key + " = ... } = obj); use ':' in an object literal");
  }
  if (anySpread && anyMethod) {
    for (auto& p : e->props)
      if (p.method) return errT(p.loc, "method shorthand in an object literal with spread properties is not supported; use a class");
  }
  if (anySpread && anyAccessor) {
    for (auto& p : e->props)
      if (p.accessor) return errT(p.loc, "accessors in an object literal with spread properties are not supported; spread into a separate literal first");
  }
  // a method (shorthand) has its own `this`: the object; it is captured from a hidden variable
  // that the object literal sets when it creates the object
  auto checkMethod = [&](ObjProp& p, VarSym* self, Type* want) -> Type* {
    pushScope();
    scope->names["this"] = self;
    Type* t = check(p.value, want);
    popScope();
    return t;
  };
  // accessors `get k() { ... }` / `set k(v) { ... }`: the getter and the setter of key k
  auto accessorOf = [&](const std::string& k, int kind) -> ObjProp* {
    for (auto& p : e->props)
      if (p.accessor == kind && p.key == k) return &p;
    return nullptr;
  };
  // checks the getter and setter of k (`want`: the property type when known, from the expected type or
  // the signatures); the property type
  auto checkAccessorPair = [&](const std::string& k, VarSym* self, Type* want) -> Type* {
    ObjProp* g = accessorOf(k, 1);
    ObjProp* st = accessorOf(k, 2);
    Type* pt = want;
    if (g) {
      Type* gt = checkMethod(*g, self, pt ? T.func({}, pt) : nullptr);
      if (gt->t == TT::Func) {
        if (gt->ret->t == TT::Void) err(g->loc, "the getter of '" + k + "' must return a value");
        else if (pt && gt->ret != pt && gt->ret->t != TT::Unknown && pt->t != TT::Unknown)
          err(g->loc, "the getter of '" + k + "' returns '" + typeName(gt->ret) + "', expected '" + typeName(pt) + "'");
        else if (!pt) pt = gt->ret;
      }
    }
    if (st) {
      Type* ft = checkMethod(*st, self, pt ? T.func({pt}, T.voidT()) : nullptr);
      if (ft->t == TT::Func && !ft->params.empty()) {
        Type* sp = ft->params[0];
        if (pt && sp != pt && sp->t != TT::Unknown && pt->t != TT::Unknown)
          err(st->loc, "the getter and setter of '" + k + "' must have the same type ('" + typeName(pt) + "' and '" + typeName(sp) + "')");
        else if (!pt) pt = sp;
      }
    }
    return pt ? pt : T.unknown();
  };
  if (anySpread) {
    // { ...a, k: v, ...b }: later entries override earlier ones; a key keeps its first position
    std::vector<std::string> names;
    std::vector<Type*> types;
    std::vector<int> src;
    std::vector<std::vector<int>> cond;
    std::vector<std::vector<std::pair<std::string, Type*>>> entries(e->props.size());
    for (size_t pi = 0; pi < e->props.size(); pi++) {
      ObjProp& p = e->props[pi];
      if (p.spread) {
        Type* t = check(p.value, nullptr);
        if (t->t == TT::Class) {
          resolveClass(t->cls);
          if (!t->cls->abstractProps.empty())
            return errT(p.loc, "spreading a value of abstract class type '" + t->cls->name +
                                   "' with abstract properties is not supported (their subclass fields are not known)");
          for (auto& f : t->cls->fields)
            if (f.name[0] != '#') {  // #private fields are not copied
              if (!spreadExact(f.name, f.type, f.optional && !f.undefOk, exp, p.loc)) return T.unknown();
              entries[pi].push_back({f.name, f.type});
            }
        } else if (t->t == TT::Object && t->shape->ownAccessors) {
          // an object literal with accessors: its properties and the values of its getters, in its order
          std::vector<std::pair<std::string, Type*>> own;
          if (!readableProps(t, p.loc, "spread", own)) return T.unknown();
          for (auto& pr : own) {
            const ObjShape::Prop* sp = t->shape->findProp(pr.first);
            if (!spreadExact(pr.first, pr.second, sp && sp->optional && !sp->undefOk, exp, p.loc)) return T.unknown();
            entries[pi].push_back(pr);
          }
        } else if (t->t == TT::Object) {
          for (auto& pr : t->shape->props) {
            if (!spreadExact(pr.name, pr.type, pr.optional && !pr.undefOk, exp, p.loc)) return T.unknown();
            entries[pi].push_back({pr.name, pr.type});
          }
        } else if (t->t != TT::Unknown) {
          return errT(p.loc, "only objects can be spread into an object literal (got '" + typeName(t) + "')");
        }
        continue;
      }
      const ObjShape::Prop* sp = exp && exp->t == TT::Object ? exp->shape->findProp(p.key) : nullptr;
      Type* t = check(p.value, sp ? sp->type : nullptr);
      if (sp && sp->optional && !sp->undefOk && hasUndefined(t)) { err(p.loc, exactOptionalError(p.key, sp->type, t)); continue; }
      if (sp && t->t == TT::Void) { err(p.loc, "property value cannot be void: a void function returns no value in E7 (return a value, or make the field optional and leave it out)"); continue; }
      if (sp) coerce(p.value, sp->type);
      else if (t->t == TT::Null || t->t == TT::Undefined || t->t == TT::Void) {
        err(p.loc, "cannot infer the type of property '" + p.key + "'");
        t = T.unknown();
      }
      entries[pi].push_back({p.key, sp ? sp->type : t});
    }
    spreadUpsert(e->props, entries, names, types, src, cond);
    spreadLits.push_back({e, entries});
    std::vector<std::pair<std::string, Type*>> fields;
    if (exp && exp->t == TT::Object) {
      ObjShape* sh = exp->shape;
      if (!sh->methods.empty()) return errT(e->loc, "object literals cannot implement '" + typeName(exp) + "' (it has methods)");
      for (size_t i = 0; i < names.size(); i++) {
        const ObjShape::Prop* sp = sh->findProp(names[i]);
        Type* ft = sp ? sp->type : types[i];
        if (sp && types[i] != ft && !(kindOf(types[i]) == K_R && kindOf(ft) == K_R && !isBoxedOptional(types[i]) &&
                                      !isBoxedOptional(ft) && assignable(types[i], ft)) &&
            !assignable(types[i], ft))
          err(e->loc, "property '" + names[i] + "' of type '" + typeName(types[i]) + "' is not assignable to '" + typeName(ft) + "'");
        fields.push_back({names[i], ft});
      }
      for (auto& sp : sh->props) {
        bool present = false;
        for (auto& n : names) if (n == sp.name) present = true;
        if (present) continue;
        if (!sp.optional && !(sp.type->t == TT::Optional && sp.type->hasUndef))
          err(e->loc, "property '" + sp.name + "' is missing in object literal of type '" + typeName(exp) + "'");
        fields.push_back({sp.name, sp.type});
        src.push_back(-1);
        cond.emplace_back();
      }
      e->cls = recordFor(fields)->cls;
      e->fieldSrc = src;
      e->fieldCond = cond;
      return exp;
    }
    if (exp && exp->t != TT::Unknown) return errT(e->loc, "an object literal is not assignable to type '" + typeName(exp) + "'");
    std::vector<ObjShape::Prop> props;
    for (size_t i = 0; i < names.size(); i++) {
      props.push_back({names[i], types[i], false});
      fields.push_back({names[i], types[i]});
    }
    e->cls = recordFor(fields)->cls;
    e->fieldSrc = src;
    e->fieldCond = cond;
    return T.anonObject(props);
  }
  for (size_t i = 0; i < e->props.size(); i++)
    for (size_t j = 0; j < i; j++) {
      const ObjProp& a = e->props[i];
      const ObjProp& b = e->props[j];
      if (a.key != b.key || (a.accessor && b.accessor && a.accessor != b.accessor)) continue;  // a getter and its setter
      if (a.accessor && b.accessor)
        return errT(a.loc, "an object literal cannot have multiple get/set accessors with the same name ('" + a.key + "')");
      if (a.accessor || b.accessor)
        return errT(a.loc, "an object literal cannot have a property and an accessor with the same name ('" + a.key + "')");
      return errT(a.loc, "duplicate property '" + a.key + "'");
    }
  if (exp && exp->t == TT::Object) {
    ObjShape* sh = exp->shape;
    VarSym* self = nullptr;
    if (anyMethod || anyAccessor) {
      self = newVar("this", exp, true, e->loc);
      self->owner = curFn;
    }
    std::vector<std::pair<std::string, Type*>> fields, methods;
    for (auto& p : e->props) {
      if (p.accessor) {
        // an accessor of the expected type, implemented by the literal's getter / setter (checked once,
        // at the first of the two)
        ObjProp* first = accessorOf(p.key, 1);
        ObjProp* other = accessorOf(p.key, 2);
        if (!first || (other && other < first)) std::swap(first, other);
        if (first != &p) continue;
        const ObjShape::Method* ig = sh->findMethod("get " + p.key);
        const ObjShape::Method* is = sh->findMethod("set " + p.key);
        if (const ObjShape::Prop* sp = sh->findProp(p.key)) {
          err(p.loc, "property '" + p.key + "' is an accessor in the object literal; e7 reads the properties of '" + typeName(exp) +
                         "' as fields, so declare it there as 'get " + p.key + "(): " + typeName(sp->type) + "'");
          continue;
        }
        if (!ig && !is) {
          err(p.loc, "object literal may only specify known properties; '" + p.key + "' does not exist in '" + typeName(exp) + "'");
          continue;
        }
        Type* want = ig ? ig->fn->ret : is->fn->params[0];
        checkAccessorPair(p.key, self, want);
        bool hasG = accessorOf(p.key, 1) != nullptr, hasS = accessorOf(p.key, 2) != nullptr;
        if (ig && !hasG) err(p.loc, "the object literal must implement the getter of '" + p.key + "' declared by '" + typeName(exp) + "'");
        if (is && !hasS) err(p.loc, "the object literal must implement the setter of '" + p.key + "' declared by '" + typeName(exp) + "'");
        Type* pairT = accessorPairType(want);
        tupleRecord(pairT);
        fields.push_back({p.key, pairT});
        if (hasG) methods.push_back({"get " + p.key, ig ? ig->fn : T.func({}, want)});
        if (hasS) methods.push_back({"set " + p.key, is ? is->fn : T.func({want}, T.voidT())});
        continue;
      }
      const ObjShape::Prop* sp = sh->findProp(p.key);
      const ObjShape::Method* sm = sp ? nullptr : sh->findMethod(p.key);
      if (!sp && !sm && (sh->findMethod("get " + p.key) || sh->findMethod("set " + p.key))) {
        err(p.loc, "object literals cannot implement the accessor '" + p.key + "' of '" + typeName(exp) +
                       "' with a plain property or method; write 'get " + p.key + "() { ... }'");
        continue;
      }
      if (sp) {
        Type* vt = p.method ? checkMethod(p, self, sp->type) : check(p.value, sp->type);
        if (!p.method && sp->optional && !sp->undefOk && hasUndefined(vt)) { err(p.loc, exactOptionalError(p.key, sp->type, vt)); continue; }
        if (p.method) {}
        else if (vt->t == TT::Void) { err(p.loc, "property value cannot be void: a void function returns no value in E7 (return a value, or make the field optional and leave it out)"); continue; }
        coerce(p.value, sp->type);
        fields.push_back({p.key, sp->type});
      } else if (sm) {
        // the interface's method is implemented by a function property (shorthand or arrow)
        Type* t = p.method ? checkMethod(p, self, sm->fn) : check(p.value, sm->fn);
        if (t != sm->fn && t->t != TT::Unknown)
          err(p.loc, "property '" + p.key + "' of type '" + typeName(t) + "' does not match the method '" + p.key + "' of '" +
                         typeName(exp) + "' (" + typeName(sm->fn) + ")");
        fields.push_back({p.key, sm->fn});
        methods.push_back({p.key, sm->fn});
      } else {
        err(p.loc, "object literal may only specify known properties; '" + p.key + "' does not exist in '" + typeName(exp) + "'");
      }
    }
    for (auto& sp : sh->props) {
      bool present = false;
      for (auto& p : e->props) if (p.key == sp.name) present = true;
      if (present) continue;
      if (!sp.optional && !(sp.type->t == TT::Optional && sp.type->hasUndef))
        err(e->loc, "property '" + sp.name + "' is missing in object literal of type '" + typeName(exp) + "'");
      fields.push_back({sp.name, sp.type});
    }
    for (auto& m : sh->methods) {
      bool acc = m.name.compare(0, 4, "get ") == 0 || m.name.compare(0, 4, "set ") == 0;
      std::string nm = acc ? m.name.substr(4) : m.name;
      bool present = false;
      for (auto& p : e->props) if (p.key == nm) present = true;
      if (!present)
        err(e->loc, std::string(acc ? "accessor '" : "method '") + nm + "' is missing in object literal of type '" + typeName(exp) + "'");
    }
    e->cls = recordFor(fields, false, methods.empty() ? nullptr : &methods, anyAccessor)->cls;
    e->var = self;
    return exp;
  }
  if ((anyMethod || anyAccessor) && !(exp && exp->t != TT::Unknown)) {
    // { n: 2, twice(x: number) { return x * this.n; } }: the type has the properties and methods
    // ({ n: number; twice(x: number): number }); `this` is that type
    T.shapes.emplace_back(new ObjShape());
    ObjShape* sh = T.shapes.back().get();
    Type* selfT = T.objectType(sh);
    VarSym* self = newVar("this", selfT, true, e->loc);
    self->owner = curFn;
    sh->ownAccessors = anyAccessor;
    std::vector<Type*> propT(e->props.size(), nullptr);
    for (size_t i = 0; i < e->props.size(); i++) {
      ObjProp& p = e->props[i];
      if (p.method || p.accessor) continue;
      Type* t = check(p.value, nullptr);
      if (t->t == TT::Null || t->t == TT::Undefined) {
        err(p.loc, "cannot infer the type of property '" + p.key + "' from null/undefined; declare an interface");
        t = T.unknown();
      }
      if (t->t == TT::Void) { err(p.loc, "property value cannot be void"); t = T.unknown(); }
      propT[i] = t;
      sh->props.push_back({p.key, t, false});
    }
    // methods with a complete signature are part of `this` before any body is checked
    std::vector<bool> known(e->props.size(), false);
    for (size_t i = 0; i < e->props.size(); i++) {
      ObjProp& p = e->props[i];
      if (!p.method) continue;
      Expr* a = p.value;
      bool complete = a->retType != nullptr;
      for (auto& q : a->params) complete = complete && q.type && !q.init && !q.pattern;
      if (!complete) continue;
      std::vector<Type*> ps;
      std::vector<bool> opt;
      bool rest = false;
      for (auto& q : a->params) {
        Type* qt = resolveType(q.type);
        if (q.optional) qt = T.optional(qt, false, true);
        ps.push_back(qt);
        opt.push_back(q.optional);
        rest |= q.rest;
      }
      propT[i] = T.func(ps, resolveType(a->retType), opt, rest);
      known[i] = true;
      sh->methods.push_back({p.key, propT[i]});
    }
    // accessors with a declared type (a getter's return type, or a setter's parameter type) are part
    // of `this` before any body is checked
    std::map<std::string, Type*> accT;
    for (size_t i = 0; i < e->props.size(); i++) {
      ObjProp& p = e->props[i];
      if (!p.accessor || accT.count(p.key)) continue;
      ObjProp* g = accessorOf(p.key, 1);
      ObjProp* st = accessorOf(p.key, 2);
      Type* t = nullptr;
      if (g && g->value->retType) t = resolveType(g->value->retType);
      else if (!g && st && st->value->params.size() == 1 && st->value->params[0].type) t = resolveType(st->value->params[0].type);
      if (!t) continue;
      accT[p.key] = t;
      if (g) sh->methods.push_back({"get " + p.key, T.func({}, t)});
      if (st) sh->methods.push_back({"set " + p.key, T.func({t}, T.voidT())});
    }
    std::set<std::string> accDone;
    for (size_t i = 0; i < e->props.size(); i++) {
      ObjProp& p = e->props[i];
      if (p.accessor) {
        if (!accDone.insert(p.key).second) continue;
        auto known0 = accT.find(p.key);
        bool wasKnown = known0 != accT.end();
        Type* t = checkAccessorPair(p.key, self, wasKnown ? known0->second : nullptr);
        if (!wasKnown && t->t != TT::Unknown) {
          accT[p.key] = t;
          if (accessorOf(p.key, 1)) sh->methods.push_back({"get " + p.key, T.func({}, t)});
          if (accessorOf(p.key, 2)) sh->methods.push_back({"set " + p.key, T.func({t}, T.voidT())});
        }
        continue;
      }
      if (!p.method) continue;
      Type* t = checkMethod(p, self, nullptr);
      if (!known[i]) {
        propT[i] = t;
        if (t->t == TT::Func) sh->methods.push_back({p.key, t});
      }
    }
    std::vector<std::pair<std::string, Type*>> fields, methods;
    std::set<std::string> accField;
    for (size_t i = 0; i < e->props.size(); i++) {
      ObjProp& p = e->props[i];
      if (p.accessor) {
        // one field (the accessor pair) at the position of the first of the getter and the setter
        if (!accField.insert(p.key).second) continue;
        auto it = accT.find(p.key);
        Type* pairT = accessorPairType(it != accT.end() ? it->second : T.unknown());
        tupleRecord(pairT);
        fields.push_back({p.key, pairT});
        sh->order.push_back(p.key);
        continue;
      }
      fields.push_back({p.key, propT[i]});
      if (!p.method) sh->order.push_back(p.key);
    }
    for (auto& m : sh->methods) methods.push_back({m.name, m.fn});
    e->cls = recordFor(fields, false, &methods, anyAccessor)->cls;
    e->var = self;
    return selfT;
  }
  if (exp && exp->t != TT::Unknown) {
    if (exp->t == TT::Class) return errT(e->loc, "an object literal cannot be used as class '" + typeName(exp) + "'; use 'new'");
    return errT(e->loc, "an object literal is not assignable to type '" + typeName(exp) + "'");
  }
  std::vector<ObjShape::Prop> props;
  std::vector<std::pair<std::string, Type*>> fields;
  for (auto& p : e->props) {
    Type* t = check(p.value, nullptr);
    if (t->t == TT::Null || t->t == TT::Undefined) {
      err(p.loc, "cannot infer the type of property '" + p.key + "' from null/undefined; declare an interface");
      t = T.unknown();
    }
    if (t->t == TT::Void) { err(p.loc, "property value cannot be void"); t = T.unknown(); }
    props.push_back({p.key, t, false});
    fields.push_back({p.key, t});
  }
  e->cls = recordFor(fields)->cls;
  return T.anonObject(props);
}

// ------------------------------------------------------------------ operators
Type* Checker::checkUnary(Expr* e, Type* expected) {
  const std::string& op = e->raw;
  if (op == "void") {
    check(e->a, nullptr);  // evaluated for its effects; the value is undefined
    return T.undef();
  }
  if (op == "!") {
    checkCond(e->a);
    return T.boolean();
  }
  if (op == "-") {
    Type* exp = expected ? baseOf(expected) : nullptr;
    Expr* x = strip(e->a);
    if (exp && exp->t == TT::Int && x->k == EK::Num && x->isIntLit) {
      std::string why;
      int64_t v;
      x->type = exp;
      if (!litFits(x, exp, v, true, why)) return errT(e->loc, why);
      e->ival = v;
      e->intrinsic = IN_CONST_NUM;  // folded integer constant
      return exp;
    }
    Type* t = check(e->a, exp && (exp->t == TT::Number || exp->t == TT::BigInt) ? exp : nullptr);
    if (t->t == TT::Number || t->t == TT::Int || t->t == TT::BigInt || t->t == TT::Unknown) return t;
    if (t->t == TT::Enum && !t->en->isString) return T.number();
    return errT(e->loc, "operator '-' cannot be applied to type '" + typeName(t) + "'");
  }
  if (op == "+") {
    Type* t = check(e->a, nullptr);
    if (t->t == TT::Number || t->t == TT::String || t->t == TT::Bool || t->t == TT::Unknown) return T.number();
    if (t->t == TT::Enum && !t->en->isString) return T.number();
    if (t->t == TT::Int) return errT(e->loc, "unary '+' on '" + typeName(t) + "' is not supported; use Number(x)");
    return errT(e->loc, "unary '+' cannot be applied to type '" + typeName(t) + "'");
  }
  if (op == "~") {
    Type* t = check(e->a, nullptr);
    if (t->t == TT::Number || t->t == TT::Int || t->t == TT::BigInt || t->t == TT::Unknown) return t;
    return errT(e->loc, "operator '~' cannot be applied to type '" + typeName(t) + "'");
  }
  return errT(e->loc, "unsupported unary operator '" + op + "'");
}

bool Checker::checkLValue(Expr* e) {
  Expr* x = strip(e);
  switch (x->k) {
    case EK::Ident: {
      Sym* s = lookup(x->raw);
      if (s && s->k == SymK::Var) {
        VarSym* v = static_cast<VarSym*>(s);
        if (v->isConst) { err(x->loc, "cannot assign to '" + x->raw + "' because it is a constant"); return false; }
        if (v->isGlobal && v->globalIdx < 0) { err(x->loc, "cannot assign to '" + x->raw + "'"); return false; }
        return true;
      }
      if (s) { err(x->loc, "cannot assign to '" + x->raw + "'"); return false; }
      err(x->loc, "cannot find name '" + x->raw + "'" + suggestName(x->raw, false));
      return false;
    }
    case EK::Member:
    case EK::Index:
      if (x->optional) {
        err(x->loc, "the left-hand side of an assignment may not be an optional property access");
        return false;
      }
      return true;
    default:
      err(x->loc, "invalid assignment target");
      return false;
  }
}

// Type of an lvalue for writing (declared, not narrowed)
static Type* storageTypeOf(Expr* x) {
  x = strip(x);
  if (x->k == EK::Ident && x->var) return x->var->type;
  return nullptr;
}

// Declared (not narrowed) type of an accessor or abstract property that is written; nullptr for
// other targets.
// exactOptionalPropertyTypes: 'x?: T' may be left out but not set to undefined; 'x?: T | undefined' may be
std::string Checker::exactOptionalError(const std::string& name, Type* declared, Type* value) {
  std::string base = typeName(declared->t == TT::Optional ? declared->elem : declared);
  return "type '" + typeName(value) + "' is not assignable to the optional property '" + name + "?: " + base +
         "' (exactOptionalPropertyTypes): leave the property out, or declare it '" + name + "?: " + base + " | undefined'";
}

// a spread copies a property that may hold undefined into an optional property that may not
bool Checker::spreadExact(const std::string& name, Type* t, bool absentIfUndef, Type* exp, Loc loc) {
  if (absentIfUndef || !hasUndefined(t) || !exp || exp->t != TT::Object) return true;
  const ObjShape::Prop* sp = exp->shape->findProp(name);
  if (!sp || !sp->optional || sp->undefOk) return true;
  err(loc, exactOptionalError(name, sp->type, t));
  return false;
}

// the optional property or field a member target writes, when its declared type leaves out undefined
const char* Checker::exactOptionalTarget(Expr* target) {
  target = strip(target);
  if (target->k != EK::Member || target->intrinsic != 0) return nullptr;
  if (target->field >= 0 && target->cls) {
    auto& f = target->cls->fields[target->field];
    return f.optional && !f.undefOk ? f.name.c_str() : nullptr;
  }
  if (target->field < 0 && target->a->type && baseOf(target->a->type)->t == TT::Object) {
    const ObjShape::Prop* p = baseOf(target->a->type)->shape->findProp(target->raw);
    return p && p->optional && !p->undefOk ? p->name.c_str() : nullptr;
  }
  return nullptr;
}

Type* Checker::declaredMemberType(Expr* target) {
  target = strip(target);
  if (target->k != EK::Member) return nullptr;
  const std::string& n = target->raw;
  switch (target->intrinsic) {
    case IN_ABSTRACT_PROP: return target->cls->abstractProp(n);
    case IN_ACCESSOR: case IN_SUPER_ACCESSOR: {
      int s = target->cls->setterSlot(n);
      if (s < 0) return nullptr;
      FuncSym* f = target->cls->vtable[s].fn;
      resolveSig(f);
      return f->type->params.empty() ? nullptr : f->type->params[0];
    }
    case IN_PRIVATE_ACCESSOR: {
      auto it = target->cls->privateMethods.find("set " + n);
      if (it == target->cls->privateMethods.end()) return nullptr;
      resolveSig(it->second);
      return it->second->type->params.empty() ? nullptr : it->second->type->params[0];
    }
    case IN_STATIC_ACCESSOR: {
      auto it = target->cls->staticMethods.find("set " + n);
      if (it == target->cls->staticMethods.end()) return nullptr;
      resolveSig(it->second);
      return it->second->type->params.empty() ? nullptr : it->second->type->params[0];
    }
    case IN_IFACE_ACCESSOR: {
      Type* ot = target->a->type ? baseOf(target->a->type) : nullptr;
      const ObjShape::Method* m = ot && ot->t == TT::Object ? ot->shape->findMethod("set " + n) : nullptr;
      return m && !m->fn->params.empty() ? m->fn->params[0] : nullptr;
    }
    default: return nullptr;
  }
}

std::string Checker::memberWriteError(Expr* target) {
  target = strip(target);
  if (target->k != EK::Member) return "";
  const std::string& n = target->raw;
  // accessors: checkAccessor already required a setter; abstract properties are writable
  if (target->intrinsic == IN_ACCESSOR || target->intrinsic == IN_STATIC_ACCESSOR ||
      target->intrinsic == IN_SUPER_ACCESSOR || target->intrinsic == IN_IFACE_ACCESSOR ||
      target->intrinsic == IN_PRIVATE_ACCESSOR ||
      target->intrinsic == IN_ABSTRACT_PROP)
    return "";
  if (target->intrinsic == IN_STATIC_FIELD) {
    Expr* base = strip(target->a);
    Sym* s = base->k == EK::Ident ? lookup(base->raw) : nullptr;
    if (!classOf(s)) return "cannot assign to '" + n + "' because it is an import";
    if (target->var && target->var->isConst) return "cannot assign to '" + n + "' because it is a read-only property";
    return "";
  }
  if (target->field >= 0 && target->cls) {
    const ClassSym::Field& f = target->cls->fields[target->field];
    // like TypeScript: a readonly field may only be assigned through `this` in its own class's constructor
    bool inOwnCtor = curFn && curFn == f.owner->ctor && strip(target->a)->k == EK::This;
    if (f.readonly && !inOwnCtor) return "cannot assign to '" + n + "' because it is a read-only property";
    return "";
  }
  if (target->intrinsic == IN_ARR_LEN) {
    Type* ot = target->a->type ? baseOf(target->a->type) : nullptr;
    if (ot && ot->ro) return "cannot assign to 'length' because it is a read-only property";
    return "";
  }
  if (target->intrinsic != 0 || !target->native.empty()) return "cannot assign to '" + n + "'";
  if (target->a->type && baseOf(target->a->type)->t == TT::Object) {
    const ObjShape::Prop* p = baseOf(target->a->type)->shape->findProp(n);
    if (p && p->readonly) return "cannot assign to '" + n + "' because it is a read-only property";
  }
  return "";
}

// an element of a readonly array or tuple
std::string Checker::indexWriteError(Expr* target) {
  target = strip(target);
  if (target->k != EK::Index || !target->a->type) return "";
  Type* ot = baseOf(target->a->type);
  if ((ot->t == TT::Array || ot->t == TT::Tuple) && ot->ro)
    return "index signature in type '" + typeName(ot) + "' only permits reading";
  return "";
}

Type* Checker::checkUpdate(Expr* e) {
  if (!checkLValue(e->a)) return T.unknown();
  Type* t = checkTarget(e->a, 2);
  // built-in / intrinsic properties (array length, Map.size, Math.PI, static fields, ...) are not
  // writable through ++ / --, consistent with assignment
  Expr* target = strip(e->a);
  if (target->k == EK::Member && target->intrinsic == IN_ARR_LEN && target->a->type &&
      baseOf(target->a->type)->t == TT::Array)
    return errT(e->loc, "'" + e->raw + "' on an array length is not supported; write 'a.length = a.length " +
                            e->raw.substr(0, 1) + " 1' instead");
  std::string werr = memberWriteError(target);
  if (werr.empty()) werr = indexWriteError(target);
  if (!werr.empty()) return errT(e->loc, werr);
  Type* st = storageTypeOf(e->a);
  if (Type* dt = declaredMemberType(target)) {
    st = dt;
    e->a->type = dt;
  }
  if (st) t = st;
  if (t->t == TT::Number || t->t == TT::Int || t->t == TT::BigInt || t->t == TT::Unknown) {
    std::string key = pathKey(e->a);
    invalidatePath(key);
    return t;
  }
  return errT(e->loc, "operator '" + e->raw + "' requires a number or integer operand");
}

static bool isStringy(Type* t) { return t->t == TT::String || (t->t == TT::Enum && t->en->isString); }
static bool isNumberish(Type* t) { return t->t == TT::Number || (t->t == TT::Enum && !t->en->isString); }

Type* Checker::checkBinary(Expr* e, Type* expected) {
  const std::string& op = e->raw;
  if (op == ",") {
    check(e->a, nullptr);  // evaluated for its effects
    return check(e->b, expected);
  }
  if (op == "instanceof") {
    Type* lt = check(e->a, nullptr);
    Expr* r = strip(e->b);
    if (r->k != EK::Ident) return errT(e->b->loc, "right side of 'instanceof' must be a class name");
    if ((r->raw == "Error" || r->raw == "TypeError" || r->raw == "RangeError" || r->raw == "SyntaxError" ||
         r->raw == "URIError" || r->raw == "AggregateError") && (!lookup(r->raw) || lookup(r->raw)->k == SymK::Namespace)) {
      if (lt->t != TT::Caught && lt->t != TT::Error && lt->t != TT::Unknown && !(lt->t == TT::Optional && lt->elem->t == TT::Error))
        return errT(e->a->loc, "'instanceof " + r->raw + "' applies to a caught value or an Error");
      e->intrinsic = IN_ERROR_IS;
      e->str = utf8ToUtf16(r->raw);
      return T.boolean();
    }
    Sym* s = lookup(r->raw);
    if (s && s->k == SymK::Generic && static_cast<GenericSym*>(s)->kind == GenericSym::Class)
      return errT(e->b->loc, "'instanceof " + r->raw + "' is not supported: '" + r->raw +
                                 "' is a generic class, and e7 compiles one class per type argument list");
    if (s && s->k == SymK::Alias && static_cast<AliasSym*>(s)->typeOnlyImport)
      return errT(e->b->loc, "'" + r->raw + "' cannot be used as a value because it was imported using 'import type'");
    if (!s || s->k != SymK::Class) return errT(e->b->loc, "'" + r->raw + "' is not a class");
    Type* b = baseOf(lt);
    if (b->t != TT::Class && b->t != TT::Object && b->t != TT::Unknown)
      return errT(e->a->loc, "left side of 'instanceof' must be an object");
    e->cls = static_cast<ClassSym*>(s);
    resolveClass(e->cls);
    return T.boolean();
  }
  bool lLit = isIntLiteralExpr(e->a), rLit = isIntLiteralExpr(e->b);
  Type *lt, *rt;
  bool isShift = op == "<<" || op == ">>" || op == ">>>";
  if (lLit && !rLit && !isShift) {
    rt = check(e->b, nullptr);
    lt = check(e->a, baseOf(rt)->t == TT::Int ? baseOf(rt) : nullptr);
  } else if (rLit && !lLit) {
    lt = check(e->a, nullptr);
    rt = check(e->b, baseOf(lt)->t == TT::Int ? baseOf(lt) : nullptr);
  } else if (lLit && rLit) {
    Type* exp = expected ? baseOf(expected) : nullptr;
    bool intCtx = exp && exp->t == TT::Int && op != "<" && op != ">" && op != "<=" && op != ">=" && op != "===" && op != "!==";
    lt = check(e->a, intCtx ? exp : nullptr);
    rt = check(e->b, intCtx ? exp : nullptr);
  } else {
    lt = check(e->a, nullptr);
    rt = check(e->b, nullptr);
  }
  if (lt->t == TT::Unknown || rt->t == TT::Unknown) return op == "===" || op == "!==" || op == "<" || op == ">" ? T.boolean() : T.unknown();
  auto mismatch = [&]() {
    if ((lt->t == TT::Int && rt->t == TT::Number) || (lt->t == TT::Number && rt->t == TT::Int))
      return errT(e->loc, "cannot mix '" + typeName(lt) + "' and '" + typeName(rt) + "' in '" + op +
                              "'; convert explicitly (e.g. u32(x) or Number(x))");
    if (lt->t == TT::Int && rt->t == TT::Int)
      return errT(e->loc, "cannot mix integer types '" + typeName(lt) + "' and '" + typeName(rt) + "'; convert explicitly");
    return errT(e->loc, "operator '" + op + "' cannot be applied to types '" + typeName(lt) + "' and '" + typeName(rt) + "'");
  };
  if (op == "+") {
    if (isStringy(lt) || isStringy(rt)) {
      Type* other = isStringy(lt) ? rt : lt;
      if (other->t == TT::Void) return errT(e->loc, "cannot concatenate a void value");
      if (other->t == TT::Caught) return errT(e->loc, "the caught value is of type 'unknown'; use String(e) or narrow it with 'instanceof Error'");
      e->intrinsic = IN_STR_CONCAT;
      return T.string();
    }
    if (isNumberish(lt) && isNumberish(rt)) return T.number();
    if (lt->t == TT::Int && lt == rt) return lt;
    if (lt->t == TT::BigInt && rt->t == TT::BigInt) return T.bigint();
    return mismatch();
  }
  if (op == "-" || op == "*" || op == "/" || op == "%" || op == "**") {
    if (isNumberish(lt) && isNumberish(rt)) return T.number();
    if (lt->t == TT::Int && lt == rt) {
      if (op == "**") return errT(e->loc, "'**' is not supported for integer types; use Number(x) ** y");
      return lt;
    }
    if (lt->t == TT::BigInt && rt->t == TT::BigInt) return T.bigint();
    return mismatch();
  }
  if (op == "&" || op == "|" || op == "^") {
    if (isNumberish(lt) && isNumberish(rt)) return T.number();
    if (lt->t == TT::Int && lt == rt) return lt;
    if (lt->t == TT::BigInt && rt->t == TT::BigInt) return T.bigint();
    if (lt->t == TT::Bool && rt->t == TT::Bool) return errT(e->loc, "bitwise operators on booleans are not allowed; use && / ||");
    return mismatch();
  }
  if (isShift) {
    if (isNumberish(lt) && isNumberish(rt)) return T.number();
    if (lt->t == TT::Int && (rt->t == TT::Int || rt->t == TT::Number)) return lt;  // shift count may be any number
    if (lt->t == TT::Int && rLit) return lt;
    if (lt->t == TT::BigInt && rt->t == TT::BigInt) {
      if (op == ">>>") return errT(e->loc, "'>>>' is not allowed on bigint");
      return T.bigint();
    }
    return mismatch();
  }
  if (op == "<" || op == ">" || op == "<=" || op == ">=") {
    if (isNumberish(lt) && isNumberish(rt)) return T.boolean();
    if (lt->t == TT::Int && lt == rt) return T.boolean();
    if (isStringy(lt) && isStringy(rt)) return T.boolean();
    if (lt->t == TT::BigInt && rt->t == TT::BigInt) return T.boolean();
    if (lt->t == TT::Date && rt->t == TT::Date) { e->intrinsic = IN_DATE_CMP; return T.boolean(); }
    return mismatch();
  }
  if (op == "===" || op == "!==" || op == "==" || op == "!=") {
    bool loose = op == "==" || op == "!=";
    bool lNull = isNullish(lt), rNull = isNullish(rt);
    if (loose && !lNull && !rNull) return errT(e->loc, "use '" + std::string(op == "==" ? "===" : "!==") + "' instead of '" + op + "'");
    if (lNull || rNull) {
      // tsc accepts comparing any value with null / undefined (also a variable narrowed to a non-nullable
      // type, or a non-nullable one): the result is then known, but the program is valid
      Type* other = lNull ? rt : lt;
      if (other->t == TT::Void) return errT(e->loc, "an expression of type 'void' cannot be compared");
      return T.boolean();
    }
    Type* lb = baseOf(lt);
    Type* rb = baseOf(rt);
    if (lb == rb) return T.boolean();
    // a caught value (always an Error at run time) compared with an Error: reference equality
    if ((lb->t == TT::Caught || lb->t == TT::Error) && (rb->t == TT::Caught || rb->t == TT::Error)) return T.boolean();
    if (isNumberish(lb) && isNumberish(rb)) return T.boolean();
    if (isStringy(lb) && isStringy(rb)) return T.boolean();
    if (kindOf(lb) == K_R && kindOf(rb) == K_R && (assignable(lb, rb) || assignable(rb, lb))) return T.boolean();
    return errT(e->loc, "this comparison appears to be unintentional because the types '" + typeName(lt) + "' and '" +
                            typeName(rt) + "' have no overlap");
  }
  return errT(e->loc, "unsupported operator '" + op + "'");
}

Type* Checker::checkLogical(Expr* e, Type* expected) {
  const std::string& op = e->raw;
  bool condCtx = expected == T.boolean();
  if (op == "&&" || op == "||") {
    Type* lt = check(e->a, condCtx ? T.boolean() : nullptr);
    std::vector<Fact> facts;
    factsFrom(e->a, op == "&&", facts);
    pushScope();
    applyFacts(facts);
    Type* rt = check(e->b, condCtx ? T.boolean() : (op == "||" ? baseOf(lt) : nullptr));
    popScope();
    if (lt->t == TT::Void || rt->t == TT::Void) return errT(e->loc, "void value in logical expression");
    if (lt->t == TT::Bool && rt->t == TT::Bool) return T.boolean();
    if (condCtx) { e->intrinsic = 1; return T.boolean(); }  // truthiness only
    if (op == "&&") return errT(e->loc, "'&&' on non-boolean values is only supported in conditions");
    // value semantics: a || b
    Type* lb = baseOf(lt);
    Type* u = unify(lb, rt, e->loc, "'||' expression");
    if (kindOf(lb) != K_R && lt->t == TT::Optional && baseOf(u) != lb) return errT(e->loc, "incompatible '||' operands");
    if (rt != u) coerce(e->b, u);
    e->intrinsic = 2;  // value semantics
    return u;
  }
  if (op == "??") {
    Type* lt = check(e->a, nullptr);
    Type* lb = baseOf(lt);
    Type* rt = check(e->b, lb);
    if (lt->t == TT::Void || rt->t == TT::Void) return errT(e->loc, "void value in '??' expression");
    if (lt->t != TT::Optional && !isNullish(lt)) {
      // allowed but pointless; result is left type
      return lt;
    }
    if (isNullish(lt)) return rt;
    Type* res;
    if (rt->t == TT::Optional || isNullish(rt)) res = unify(lb, rt, e->loc, "'??' expression");
    else res = unify(lb, rt, e->loc, "'??' expression");
    if (rt != res) coerce(e->b, res);
    return res;
  }
  return errT(e->loc, "unsupported operator '" + op + "'");
}

Type* Checker::checkAssign(Expr* e) {
  const std::string& op = e->raw;
  Expr* target = strip(e->a);
  if (target->k == EK::Object)
    return errT(e->loc, "an object destructuring assignment must be a statement of its own: ({ a, b } = obj);");
  if (target->k == EK::Array) {
    if (op != "=") return errT(e->loc, "invalid destructuring assignment");
    std::vector<Type*> tts;
    for (Expr*& x : target->list) {
      if (x->k == EK::Spread)
        return errT(x->loc, "rest elements in destructuring assignments are not supported; use a declaration: const [a, ...rest] = xs");
      if (strip(x)->k == EK::Assign)
        return errT(x->loc, "default values in destructuring assignments are not supported; use a declaration: const [a = 1] = xs");
      if (!checkLValue(x)) return T.unknown();
      Type* t = checkTarget(x, 1);
      Type* st = storageTypeOf(x);
      tts.push_back(st ? st : t);
      invalidatePath(pathKey(x));
    }
    Expr* rhs = strip(e->b);
    if (rhs->k == EK::Array) {
      if (rhs->list.size() != target->list.size()) return errT(e->loc, "destructuring arity mismatch");
      for (size_t i = 0; i < rhs->list.size(); i++) {
        if (rhs->list[i]->k == EK::Spread) return errT(rhs->loc, "spread not allowed here");
        check(rhs->list[i], tts[i]);
        coerce(rhs->list[i], tts[i]);
      }
      rhs->type = T.voidT();
      e->intrinsic = 1;  // parallel assignment, no allocation
      return T.voidT();
    }
    Type* rt = check(e->b, nullptr);
    for (size_t i = 0; i < tts.size(); i++) {
      Type* et = rt->t == TT::Array ? rt->elem : rt->t == TT::Tuple && i < rt->elems.size() ? rt->elems[i] : nullptr;
      if (!et) return errT(e->loc, "cannot destructure '" + typeName(rt) + "'");
      if (et != tts[i] && !assignable(et, tts[i])) err(e->loc, "type '" + typeName(et) + "' is not assignable to '" + typeName(tts[i]) + "'");
    }
    return T.voidT();
  }
  if (!checkLValue(e->a)) return T.unknown();
  Type* lt;
  // target type for writing: declared type of variables (not narrowed)
  if (target->k == EK::Ident) {
    check(e->a, nullptr);
    lt = target->var ? (target->var->isGlobal ? globalVarType(target->var) : target->var->type) : T.unknown();
    if (target->var) e->a->type = lt;
  } else {
    lt = checkTarget(e->a, e->raw == "=" ? 1 : 2);
    if (Type* dt = declaredMemberType(target)) lt = dt;
    else if (target->k == EK::Member && target->field >= 0 && target->cls) lt = target->cls->fields[target->field].type;
    else if (target->k == EK::Member && target->field < 0 && target->intrinsic == 0 && target->a->type &&
             baseOf(target->a->type)->t == TT::Object) {
      const ObjShape::Prop* p = baseOf(target->a->type)->shape->findProp(target->raw);
      if (p) lt = p->type;
    }
    if (target->k == EK::Member && target->intrinsic == IN_ARR_LEN) {
      // only arrays have a writable length (string / Uint8Array length is read-only, as in TypeScript)
      Type* ot = target->a->type ? baseOf(target->a->type) : nullptr;
      if (!ot || ot->t != TT::Array || ot->ro)
        return errT(e->loc, "cannot assign to 'length' because it is a read-only property");
      if (op != "=")
        return errT(e->loc, "'" + op + "' on an array length is not supported; write 'a.length = a.length " +
                                op.substr(0, op.size() - 1) + " ...' instead");
      target->intrinsic = IN_ARR_LENGTH_SET;
    } else if (target->k == EK::Member && target->intrinsic == IN_NATIVE && target->native == "RegExp.lastIndex") {
      if (op != "=") return errT(e->loc, "'" + op + "' on lastIndex is not supported; write 're.lastIndex = re.lastIndex " + op.substr(0, op.size() - 1) + " ...' instead");
      target->intrinsic = IN_LASTINDEX_SET;
    } else if (target->k == EK::Member) {
      std::string werr = memberWriteError(target);
      if (!werr.empty()) return errT(e->loc, werr);
    } else {
      std::string werr = indexWriteError(target);
      if (!werr.empty()) return errT(e->loc, werr);
    }
    if (target->k == EK::Index && target->a->type && baseOf(target->a->type)->t == TT::String)
      return errT(e->loc, "strings are immutable");
    e->a->type = lt;
  }
  std::string key = pathKey(e->a);
  if (op == "=") {
    Type* rt = check(e->b, lt);
    if (const char* on = target->k == EK::Member ? exactOptionalTarget(target) : nullptr; on && hasUndefined(rt))
      return errT(e->loc, exactOptionalError(on, lt, rt));
    coerce(e->b, lt);
    invalidatePath(key);
    if (lt->t == TT::Optional && rt->t != TT::Unknown && nonNullValue(e->b)) setNarrow(key, rt == lt->elem ? lt->elem : baseOf(lt));
    return lt;
  }
  if (op == "||=" || op == "&&=") {
    // a ||= b: assigns when a is falsy; a &&= b: when a is truthy (the target keeps its type)
    if (lt->t == TT::Void) return errT(e->loc, "'" + op + "' requires a value");
    Type* rt = check(e->b, lt);
    coerce(e->b, lt);
    invalidatePath(key);
    // x ||= v with a non-null v: x is non-null afterwards (a truthy old value, or v)
    if (op == "||=" && lt->t == TT::Optional && rt->t != TT::Optional && !isNullish(rt) && rt->t != TT::Unknown)
      setNarrow(key, lt->elem);
    return lt;
  }
  if (op == "??=") {
    if (lt->t != TT::Optional) return errT(e->loc, "'??=' requires a nullable target");
    Type* rt = check(e->b, lt->elem);
    coerce(e->b, lt);
    invalidatePath(key);
    if (rt->t != TT::Optional && !isNullish(rt)) setNarrow(key, lt->elem);  // non-null afterwards
    return lt;
  }
  // compound: lt op rhs
  std::string bop = op.substr(0, op.size() - 1);
  bool isShift = bop == "<<" || bop == ">>" || bop == ">>>";
  Type* rt = check(e->b, lt->t == TT::Int ? lt : nullptr);
  Type* res = nullptr;
  if (bop == "+" && isStringy(lt)) {
    if (rt->t == TT::Void) return errT(e->loc, "cannot concatenate a void value");
    res = T.string();
  } else if (isNumberish(lt) && isNumberish(rt)) {
    res = T.number();
  } else if (lt->t == TT::Int && (rt == lt || (isShift && (rt->t == TT::Int || rt->t == TT::Number)))) {
    if (bop == "**") return errT(e->loc, "'**' is not supported for integer types");
    res = lt;
  } else if (lt->t == TT::BigInt && rt->t == TT::BigInt) {
    res = T.bigint();
  } else if (lt->t != TT::Unknown && rt->t != TT::Unknown) {
    return errT(e->loc, "operator '" + op + "' cannot be applied to types '" + typeName(lt) + "' and '" + typeName(rt) + "'");
  } else {
    res = lt;
  }
  if (res != lt && !(lt->t == TT::Enum)) {
    if (!assignable(res, lt)) return errT(e->loc, "result of '" + op + "' is not assignable to '" + typeName(lt) + "'");
  }
  invalidatePath(key);
  return lt;
}

// a value that is never null or undefined: a literal, `new`, a function, a conditional whose branches both are,
// or an expression whose own (narrowed) type is not nullable; conversions to the target type are looked through
bool Checker::nonNullValue(Expr* x) {
  while (x && (x->k == EK::Paren || x->k == EK::Conv)) x = x->a;
  if (!x) return false;
  switch (x->k) {
    case EK::Array: case EK::Object: case EK::Num: case EK::Str: case EK::Template: case EK::New: case EK::Arrow:
    case EK::Bool: case EK::BigLit:
      return true;
    case EK::Cond: return nonNullValue(x->b) && nonNullValue(x->c);
    default: break;
  }
  Type* t = x->type;
  return t && t->t != TT::Optional && !isNullish(t) && t->t != TT::Unknown && t->t != TT::Never && t->t != TT::Void;
}

Type* Checker::checkCondExpr(Expr* e, Type* expected) {
  checkCond(e->a);
  std::vector<Fact> tf, ff;
  factsFrom(e->a, true, tf);
  factsFrom(e->a, false, ff);
  // c ? [x] : []: an empty array literal takes the type of the other branch
  auto isEmptyArr = [](Expr* x) {
    x = strip(x);
    return x->k == EK::Array && x->list.empty();
  };
  bool emptyB = !expected && isEmptyArr(e->b) && !isEmptyArr(e->c);
  Type *bt = nullptr, *ct = nullptr;
  if (!emptyB) {
    pushScope();
    applyFacts(tf);
    bt = check(e->b, expected);
    popScope();
  }
  pushScope();
  applyFacts(ff);
  ct = check(e->c, expected ? expected : (isEmptyArr(e->c) && bt && baseOf(bt)->t == TT::Array ? baseOf(bt) : nullptr));
  popScope();
  if (emptyB) {
    pushScope();
    applyFacts(tf);
    bt = check(e->b, baseOf(ct)->t == TT::Array ? baseOf(ct) : nullptr);
    popScope();
  }
  Type* t;
  if (expected && expected->t != TT::Bool) {
    if (assignable(bt, expected) && assignable(ct, expected)) t = expected;
    else t = unify(bt, ct, e->loc, "conditional expression");
  } else {
    t = unify(bt, ct, e->loc, "conditional expression");
  }
  coerce(e->b, t);
  coerce(e->c, t);
  return t;
}

// ------------------------------------------------------------------ narrowing facts
void Checker::factsFrom(Expr* c, bool whenTrue, std::vector<Fact>& out) {
  c = strip(c);
  if (c->k == EK::Unary && c->raw == "!") { factsFrom(c->a, !whenTrue, out); return; }
  if (c->k == EK::Logical && c->raw == "&&") {
    if (whenTrue) { factsFrom(c->a, true, out); factsFrom(c->b, true, out); }
    return;
  }
  if (c->k == EK::Logical && c->raw == "||") {
    if (!whenTrue) { factsFrom(c->a, false, out); factsFrom(c->b, false, out); }
    return;
  }
  if (c->k == EK::Binary && (c->raw == "===" || c->raw == "!==" || c->raw == "==" || c->raw == "!=")) {
    Expr* l = strip(c->a);
    Expr* r = strip(c->b);
    // a literal-typed path compared with a constant: x === "a" narrows x to "a", x !== "a" to the others
    for (int side = 0; side < 2; side++) {
      Expr* path = side == 0 ? l : r;
      Expr* other = side == 0 ? r : l;
      Type* pt = path->type;
      if (!pt || !pt->lit) continue;
      Type* v = constLit(other);
      if (!v) continue;
      std::string key = pathKey(path);
      if (key.empty()) continue;
      bool eq = c->raw[0] == '=';
      if (eq == whenTrue) {
        if (v->lit && baseLit(v) == baseLit(pt) && v->strLits.size() + v->numLits.size() + (v->boolLits ? 1 : 0) == 1) out.push_back({key, v});
      } else if (v->strLits.size() + v->numLits.size() + (v->boolLits == 1 || v->boolLits == 2 ? 1 : 0) == 1) {
        Type* rest = litMinus(pt, v);
        out.push_back({key, rest ? rest : T.never()});
      }
      return;
    }
    Expr* path = nullptr;
    Expr* lit = nullptr;
    if (r->k == EK::Null || r->k == EK::Undefined) { path = l; lit = r; }
    else if (l->k == EK::Null || l->k == EK::Undefined) { path = r; lit = l; }
    if (!path || !path->type || path->type->t != TT::Optional) return;
    std::string key = pathKey(path);
    if (key.empty()) return;
    bool eq = c->raw[0] == '=';
    bool loose = c->raw.size() == 2;
    Type* t = path->type;
    // compute type when the comparison is false for '=='/'===' (i.e. value is not the literal)
    Type* notLit;
    if (loose) notLit = t->elem;
    else if (lit->k == EK::Null) notLit = T.optional(t->elem, false, t->hasUndef);
    else notLit = T.optional(t->elem, t->hasNull, false);
    if (eq != whenTrue) out.push_back({key, notLit});
    return;
  }
  if (c->k == EK::Binary && c->raw == "instanceof" && c->intrinsic == IN_ERROR_IS) {
    if (!whenTrue) return;
    std::string key = pathKey(c->a);
    if (!key.empty()) out.push_back({key, T.error()});
    return;
  }
  if (c->k == EK::Binary && c->raw == "instanceof") {
    if (!whenTrue || !c->cls) return;
    std::string key = pathKey(c->a);
    if (!key.empty()) out.push_back({key, c->cls->type});
    return;
  }
  // r.done of an IteratorResult { value: T | undefined; done: boolean }: false means r.value is a T
  if (!whenTrue && c->k == EK::Member && c->raw == "done" && c->a->type && c->a->type->t == TT::Object) {
    const ObjShape* sh = c->a->type->shape;
    if (sh->props.size() == 2 && sh->props[0].name == "value" && sh->props[1].name == "done" &&
        sh->props[0].type->t == TT::Optional && sh->name.empty()) {
      std::string base = pathKey(c->a);
      if (!base.empty()) out.push_back({base + ".value", sh->props[0].type->elem});
    }
    return;
  }
  // a call of a type predicate `x is T`: true means the argument is a T (false: for a nullable
  // argument and its base type, null / undefined)
  if (c->k == EK::Call && c->callee && c->callee->predParam == -2 && c->callee->predType && !c->callee->asserts &&
      strip(c->a)->k == EK::Member) {
    // `this is T`: the receiver
    if (!whenTrue) return;
    std::string key = pathKey(strip(c->a)->a);
    if (!key.empty()) out.push_back({key, c->callee->predType});
    return;
  }
  if (c->k == EK::Call && c->callee && c->callee->predParam >= 0 && c->callee->predType && !c->callee->asserts &&
      (size_t)c->callee->predParam < c->list.size()) {
    Expr* arg = c->list[(size_t)c->callee->predParam];
    while (arg->k == EK::Conv || arg->k == EK::Paren) arg = arg->a;
    std::string key = pathKey(arg);
    Type* at = arg->type;
    Type* pt = c->callee->predType;
    if (key.empty() || !at) return;
    if (whenTrue) out.push_back({key, pt});
    else if (at->t == TT::Optional && baseOf(pt) == at->elem && pt->t != TT::Optional)
      out.push_back({key, at->hasNull && at->hasUndef ? T.optional(T.nullT(), true, true) : at->hasNull ? T.nullT() : T.undef()});
    else if (at->lit && pt->lit && baseLit(at) == baseLit(pt)) {
      if (Type* rest = litMinus(at, pt)) out.push_back({key, rest});
    }
    return;
  }
  // truthiness of a nullable reference
  if (whenTrue && c->type && c->type->t == TT::Optional) {
    std::string key = pathKey(c);
    if (!key.empty()) out.push_back({key, c->type->elem});
  }
}

void Checker::applyFacts(const std::vector<Fact>& facts) {
  for (auto& f : facts) setNarrow(f.key, f.t);
}

// ------------------------------------------------------------------ arrow functions
Type* Checker::checkArrow(Expr* e, Type* expected) {
  Type* exp = expected ? baseOf(expected) : nullptr;
  if (exp && exp->t != TT::Func) exp = nullptr;
  FuncSym* f = e->fn;
  if (!f) {
    std::string nm = e->raw.empty() ? "<anonymous>" : e->raw;
    f = newFunc(nm, &e->params, e->retType, e->body, e->exprBody);
    f->isGenerator = e->isGenerator;
    f->isAsync = e->isAsync;
    f->arrow = e;
    f->parent = curFn;
    f->defScope = scope;
    if (e->intrinsic == IN_FUNC_EXPR && !e->raw.empty()) {
      // `function name(...) { ... name(...) ... }`: the name is in scope in the body only
      Scope* s0 = scope;
      pushScope();
      VarSym* sv = newVar(e->raw, nullptr, true, e->loc);
      sv->owner = curFn;
      scope->names[e->raw] = sv;
      f->exprSelf = sv;
      f->defScope = scope;
      scope = s0;
    }
    f->isArrowThis = e->intrinsic != IN_FUNC_EXPR && e->intrinsic != IN_OBJ_METHOD;
    f->qualName = (curFn && !curFn->isInit ? curFn->qualName + "." : "") + nm;
    e->fn = f;
  }
  // contextual parameter types
  std::vector<Type*> ps;
  std::vector<bool> opt;
  bool rest = false;
  for (size_t i = 0; i < e->params.size(); i++) {
    Param& p = e->params[i];
    Type* t = nullptr;
    Type* ctx = exp && i < exp->params.size() ? exp->params[i] : nullptr;
    if (ctx && ctx->t == TT::Unknown) ctx = nullptr;  // not known yet (type argument inference)
    // `(x: unknown) => ...` as a callback: TypeScript lets the body do nothing type-specific with x,
    // so it takes the contextual parameter type (e.g. Array.from({ length: n }, (_: unknown, i) => i))
    bool unknownAnn = p.type && p.type->k == TNK::Ref && p.type->name == "unknown" && p.type->args.empty();
    if (unknownAnn && ctx && !p.optional && !p.rest) t = ctx;
    else if (p.type) t = resolveType(p.type);
    else if (ctx) t = ctx;
    else if (p.init) {
      // type of the default value; it may refer to the earlier parameters
      Scope* s0 = scope;
      pushScope(true);
      for (size_t k = 0; k < i; k++) {
        Param& q = e->params[k];
        if (q.pattern) continue;
        VarSym* tv = newVar(q.name, q.innerType ? q.innerType : ps[k], false, q.loc);
        tv->owner = f;
        scope->names[q.name] = tv;
      }
      t = widen(check(p.init, nullptr));
      scope = s0;
      if (t->t == TT::Null || t->t == TT::Undefined) {
        err(p.loc, "cannot infer the type of parameter '" + p.name + "' from its default value; add a type annotation");
        t = T.unknown();
      }
    } else { err(p.loc, "parameter '" + p.name + "' implicitly has an unknown type; add a type annotation"); t = T.unknown(); }
    if (p.optional && p.type) t = T.optional(t, false, true);
    if (p.rest) rest = true;
    bool isOpt = p.optional;
    if (p.init) {
      if (p.rest) err(p.loc, "a rest parameter cannot have a default value");
      if (p.optional) err(p.loc, "parameter cannot be optional and have an initializer");
      // A closure has no hidden argument count: the default applies when the argument is undefined
      // (as in JavaScript), so the parameter itself is `T | undefined` unless the contextual type
      // says it is never undefined. Inside the body it has the type without undefined.
      Type* inner = t;
      Type* outer = t;
      if (t->t == TT::Optional && t->hasUndef) {
        inner = t->hasNull ? T.optional(t->elem, true, false) : t->elem;
      } else if (!(ctx && ctx == t) && t->t != TT::Unknown) {
        outer = t->t == TT::Optional ? T.optional(t->elem, t->hasNull, true) : T.optional(t, false, true);
      }
      p.innerType = inner;
      t = outer;
      isOpt = ctx ? exp->optParams[i] : true;
    }
    ps.push_back(t);
    opt.push_back(isOpt);
  }
  if (exp && e->params.size() > exp->params.size())
    err(e->loc, "function has more parameters than expected by '" + typeName(exp) + "'");
  // accept fewer parameters than the contextual type (extra arguments are ignored)
  if (exp && !rest && !exp->restParam)
    for (size_t i = e->params.size(); i < exp->params.size(); i++) {
      if (exp->params[i]->t == TT::Unknown) break;
      ps.push_back(exp->params[i]);
      opt.push_back(exp->optParams[i]);
    }
  Type* ret = nullptr;
  if (e->retType) ret = resolveType(e->retType);
  else if (exp && exp->ret->t == TT::Void && !e->isGenerator && !e->isAsync) ret = T.voidT();
  else if (exp && exp->ret->t != TT::Unknown && exp->ret->t != TT::Void) ret = exp->ret;
  if (ret) checkCoroutineRet(f, ret, e->loc);
  f->sigResolved = true;
  f->inferRet = ret == nullptr;
  f->type = T.func(ps, ret ? ret : T.unknown(), opt, rest);
  if (f->exprSelf) f->exprSelf->type = f->type;
  // a 'function' expression with no explicit this: no this binding
  checkFunction(f);
  // if the expected type has fewer params than the arrow uses, error above; if more, codegen adapts
  return f->type;
}

// ------------------------------------------------------------------ member access
ClassSym* Checker::lexicalClass() {
  for (FuncSym* f = curFn; f; f = f->parent) {
    if (f->cls) return f->cls;
    if (f->isInit) break;
  }
  return staticInitClass;
}

// #x can only be used inside the body of the class that declares it
bool Checker::checkPrivateAccess(Loc l, const std::string& name, ClassSym* owner) {
  if (lexicalClass() == owner) return true;
  err(l, "property '" + name + "' is not accessible outside class '" + owner->name + "' because it has a private identifier");
  return false;
}

// get x() / set x(v) of class c (instance or static); nullptr when c has neither
Type* Checker::checkAccessor(Expr* e, ClassSym* c, const std::string& name, bool isStatic) {
  FuncSym* g = nullptr;
  FuncSym* s = nullptr;
  if (isStatic) {
    auto gi = c->staticMethods.find("get " + name);
    auto si = c->staticMethods.find("set " + name);
    if (gi != c->staticMethods.end()) g = gi->second;
    if (si != c->staticMethods.end()) s = si->second;
  } else if (name[0] == '#') {
    // private accessors: non-virtual, declared by the class itself or a base class
    for (ClassSym* k = c; k && !g && !s; k = k->parent) {
      auto gi = k->privateMethods.find("get " + name);
      auto si = k->privateMethods.find("set " + name);
      if (gi != k->privateMethods.end()) g = gi->second;
      if (si != k->privateMethods.end()) s = si->second;
      if (g || s) {
        if (!checkPrivateAccess(e->nameLoc, name, k)) return T.unknown();
        c = k;
      }
    }
  } else {
    if (c->getterSlot(name) >= 0) g = c->vtable[c->getterSlot(name)].fn;
    if (c->setterSlot(name) >= 0) s = c->vtable[c->setterSlot(name)].fn;
  }
  if (!g && !s) return nullptr;
  int mode = e == lvalueTarget ? lvalueMode : 0;
  if (mode != 1 && !g) return errT(e->loc, "property '" + name + "' has no getter (it only has a setter)");
  if (mode >= 1 && !s) return errT(e->loc, "cannot assign to '" + name + "' because it is a read-only property");
  Type* t;
  if (g) {
    resolveSig(g);
    if (g->inferRet) {
      if (g->checking) return errT(e->loc, "getter '" + name + "' is used recursively; add a return type annotation");
      checkFunction(g);
    }
    t = g->type->ret;
  } else {
    resolveSig(s);
    t = s->type->params.empty() ? T.unknown() : s->type->params[0];
  }
  e->cls = c;
  e->intrinsic = isStatic ? IN_STATIC_ACCESSOR : name[0] == '#' ? IN_PRIVATE_ACCESSOR : IN_ACCESSOR;
  return t;
}

// `B.x = v` where x is a static field of a base class: in JavaScript the assignment creates an own
// property x of B (the base class keeps its value), read from then on through B and its subclasses
void Checker::staticShadow(ClassSym* c, const std::string& name, VarSym* inherited) {
  if (c->staticShadows.count(name)) return;
  auto global = [&](const std::string& n, Type* t) {
    VarSym* v = newVar(n, t, false, inherited->loc);
    v->isGlobal = true;
    v->owner = nullptr;
    v->globalIdx = (int)P.globals.size();
    P.globals.push_back(v);
    return v;
  };
  ClassSym::StaticShadow sh;
  sh.has = global(c->name + "." + name + " (own)", T.boolean());
  sh.value = global(c->name + "." + name, inherited->type ? inherited->type : T.unknown());
  c->staticShadows[name] = sh;
}

// a '.' / '[' link after '?.' in the same chain: the previous link's own type (before the chain made it
// optional) must not be nullable, as in TypeScript (`a?.b.c` with an optional `b` is an error)
static bool chainedOnNullable(Expr* e) {
  if (!e->chained || !e->a) return false;
  Expr* x = e->a;
  while (x->k == EK::Paren && x->a) x = x->a;
  return x->innerType && x->innerType->t == TT::Optional;
}

Type* Checker::checkMember(Expr* e, Type* expected) {
  Expr* obj = strip(e->a);
  const std::string& name = e->raw;
  // static / namespace access
  if (obj->k == EK::Ident) {
    Sym* s = lookup(obj->raw);
    if (s && s->k == SymK::Namespace) {
      // Math.sqrt, Number.isInteger, ... as a value where a function is expected: an arrow function
      NamespaceSym* ns = static_cast<NamespaceSym*>(s);
      if (ns->builtin && expected && baseOf(expected)->t == TT::Func && !isNamespaceConstant(ns->module, name))
        if (Type* t = etaExpand(e, expected, builtinArity(ns->module + "." + name))) return t;
      return checkNamespaceMember(e, ns, name, false, expected);
    }
    if (s && s->k == SymK::Enum) {
      EnumSym* en = static_cast<EnumSym*>(s);
      resolveEnum(en);
      for (auto& m : en->members)
        if (m.name == name) {
          if (en->isString) { e->intrinsic = IN_CONST_STR; e->str = m.str; }
          else { e->intrinsic = IN_CONST_NUM; e->num = m.num; }
          return en->type;
        }
      std::vector<std::string> names;
      for (auto& m : en->members) names.push_back(m.name);
      return errT(e->loc, "enum '" + en->name + "' has no member '" + name + "'" + didYouMean(name, names));
    }
    if (ClassSym* c = classOf(s)) {
      resolveClass(c);
      if (name[0] == '#' && !checkPrivateAccess(e->nameLoc, name, c)) return T.unknown();
      // own static members first, then the base classes' (as JavaScript reads them through the prototype
      // chain); #private static members are not inherited
      for (ClassSym* k = c; k; k = name[0] == '#' ? nullptr : k->staticParent) {
        auto it = k->staticFields.find(name);
        if (it != k->staticFields.end()) {
          e->intrinsic = IN_STATIC_FIELD;
          e->var = it->second;
          if (!it->second->type) globalVarType(it->second);
          Type* t = it->second->type ? it->second->type : T.unknown();
          if (k != c) {
            // inherited: read through the classes in between, which may have their own copy
            e->cls = c;
            if (e == lvalueTarget && lvalueMode >= 1 && !it->second->isConst) staticShadow(c, name, it->second);
          } else if (!(e == lvalueTarget && lvalueMode >= 1)) {
            // a static field read as a path: narrowed like a variable (if (C.#x === null) C.#x = ...; C.#x.f)
            if (Type* n = narrowed(pathKey(e), false)) return n;
          }
          return t;
        }
        if (Type* at = checkAccessor(e, k, name, true)) return at;
        if (k->staticMethods.count(name)) return errT(e->loc, "static methods cannot be used as values; wrap in an arrow function");
      }
      return errT(e->loc, "class '" + c->name + "' has no static member '" + name + "'");
    }
  }
  if (obj->k == EK::Super) {
    // super.x: an accessor of the base class (called directly, with this)
    FuncSym* f = curFn;
    while (f && f->isArrowThis && f->parent && !f->isMethod && !f->isCtor) f = f->parent;
    if (!f || !f->cls || !f->cls->parent || f->isStatic || !(f->isMethod || f->isCtor))
      return errT(e->loc, "'super' property access is only allowed in methods of derived classes");
    ClassSym* pc = f->cls->parent;
    if (pc->fieldIdx.count(name))
      return errT(e->loc, "class field '" + name + "' defined by the parent class is not accessible in the child class via super; use this." + name);
    Type* t = checkAccessor(e, pc, name, false);
    if (!t) return errT(e->loc, "base class '" + pc->name + "' has no accessor '" + name + "'");
    if (t->t == TT::Unknown) return t;
    e->intrinsic = IN_SUPER_ACCESSOR;
    Sym* th = lookup("this");
    if (th && th->k == SymK::Var) { e->var = static_cast<VarSym*>(th); markCapture(e->var); }
    return t;
  }
  Type* ot = check(e->a, nullptr);
  if (ot->t == TT::Unknown) return T.unknown();
  if (ot->t == TT::Caught)
    return errT(e->loc, "the caught value is of type 'unknown'; narrow it first: if (" + std::string(obj->k == EK::Ident ? obj->raw : "e") +
                            " instanceof Error) { ... ." + name + " ... }");
  Type* bt = ot;
  if (ot->t == TT::Optional) {
    if (!e->optional || chainedOnNullable(e))
      return errT(e->loc, "object is possibly '" + std::string(ot->hasNull ? "null" : "undefined") + "'; use '?.' or a check");
    bt = ot->elem;
  } else if (e->optional) {
    // '?.' on non-nullable: allowed, behaves like '.'
    e->optional = false;
  }
  Type* result = nullptr;
  switch (bt->t) {
    case TT::Class: {
      ClassSym* c = bt->cls;
      resolveClass(c);
      auto it = c->fieldIdx.find(name);
      if (it != c->fieldIdx.end()) {
        if (name[0] == '#' && !checkPrivateAccess(e->nameLoc, name, c->fields[it->second].owner)) return T.unknown();
        e->field = it->second;
        e->cls = c;
        result = c->fields[it->second].type;
        break;
      }
      if (Type* at = checkAccessor(e, c, name, false)) {
        if (at->t == TT::Unknown) return at;
        result = at;
        break;
      }
      if (Type* ap = c->abstractProp(name)) {
        // abstract property: the subclass declares the field, read / written by name
        e->intrinsic = IN_ABSTRACT_PROP;
        e->cls = c;
        result = ap;
        break;
      }
      if (c->methodIdx.count(name)) return errT(e->loc, "method '" + name + "' cannot be used as a value; use an arrow function");
      for (ClassSym* k = c; k; k = k->parent)
        if (k->privateMethods.count(name)) return errT(e->loc, "method '" + name + "' cannot be used as a value; use an arrow function");
      if (name[0] == '#') return errT(e->loc, "private name '" + name + "' is not declared by class '" + c->name + "'");
      return errT(e->loc, "property '" + name + "' does not exist on type '" + c->name + "'" + suggestMember(bt, name));
    }
    case TT::Object: {
      const ObjShape::Prop* p = bt->shape->findProp(name);
      if (!p) {
        const ObjShape::Method* g = bt->shape->findMethod("get " + name);
        const ObjShape::Method* s = bt->shape->findMethod("set " + name);
        if (g || s) {
          // accessor declared by an interface: a method call by name
          int mode = e == lvalueTarget ? lvalueMode : 0;
          if (mode != 1 && !g) return errT(e->loc, "property '" + name + "' has no getter");
          if (mode >= 1 && !s) return errT(e->loc, "cannot assign to '" + name + "' because it is a read-only property");
          e->intrinsic = IN_IFACE_ACCESSOR;
          result = g ? g->fn->ret : s->fn->params[0];
          break;
        }
        if (bt->shape->findMethod(name)) return errT(e->loc, "method '" + name + "' cannot be used as a value");
        return errT(e->loc, "property '" + name + "' does not exist on type '" + typeName(bt) + "'" + suggestMember(bt, name));
      }
      result = p->type;
      break;
    }
    case TT::Tuple:
      if (name == "length") { e->intrinsic = IN_CONST_NUM; e->num = (double)bt->elems.size(); result = T.number(); break; }
      return errT(e->loc, "property '" + name + "' does not exist on tuple type");
    default:
      result = checkBuiltinMethod(e, e, bt, expected);  // property form
      if (!result) return T.unknown();
      break;
  }
  // narrowing of property paths
  if (!e->optional) {
    bool pc;
    std::string key = pathKey(e, &pc);
    Type* n = narrowed(key, false);
    if (n) result = n;
  }
  if (e->optional) {
    if (result->t == TT::Void) return errT(e->loc, "void in optional chain");
    e->innerType = result;
    return T.optional(result, false, true);
  }
  return result;
}

Type* Checker::checkIndex(Expr* e) {
  Type* ot = check(e->a, nullptr);
  // obj[K] with K a constant known at compile time: the property K (obj.K)
  Type* bo = baseOf(ot);
  if ((bo->t == TT::Object || bo->t == TT::Class) && e->b) {
    Expr* k = strip(e->b);
    bool constant = k->k == EK::Str || (k->k == EK::Ident) || k->k == EK::Member || k->k == EK::Num;
    if (constant) {
      std::string key;
      if (!constKey(e->b, key)) return T.unknown();
      Expr* obj = e->a;
      bool opt = e->optional;
      e->k = EK::Member;
      e->a = obj;
      e->b = nullptr;
      e->raw = key;
      e->nameLoc = k->loc;
      e->optional = opt;
      e->a->type = nullptr;  // checked again as the member's object
      return check(e, nullptr);
    }
  }
  if (ot->t == TT::Optional) {
    if (!e->optional || chainedOnNullable(e)) return errT(e->loc, "object is possibly null or undefined; use '?.[' or a check");
    // a?.[i]: undefined when a is null or undefined (the index is not evaluated then)
    Type* r = checkIndexOn(e, ot->elem);
    if (r->t == TT::Unknown) return r;
    if (r->t == TT::Void) return errT(e->loc, "void in optional chain");
    e->innerType = r;
    return T.optional(r, false, true);
  }
  e->optional = false;  // '?.[' on a value that is never null: a plain index
  return checkIndexOn(e, ot);
}

Type* Checker::checkIndexOn(Expr* e, Type* ot) {
  switch (ot->t) {
    case TT::Array: {
      Type* it = check(e->b, nullptr);
      if (it->t != TT::Number && it->t != TT::Int && it->t != TT::Unknown) return errT(e->b->loc, "array index must be a number or integer");
      return ot->elem;
    }
    case TT::Bytes: {
      Type* it = check(e->b, nullptr);
      if (it->t != TT::Number && it->t != TT::Int && it->t != TT::Unknown) return errT(e->b->loc, "index must be a number or integer");
      return T.number();
    }
    case TT::TypedArray: {
      Type* it = check(e->b, nullptr);
      if (it->t != TT::Number && it->t != TT::Int && it->t != TT::Unknown) return errT(e->b->loc, "index must be a number or integer");
      return taElem(ot);
    }
    case TT::String: {
      Type* it = check(e->b, nullptr);
      if (it->t != TT::Number && it->t != TT::Int && it->t != TT::Unknown) return errT(e->b->loc, "index must be a number");
      return T.string();
    }
    case TT::Tuple: {
      Expr* ix = strip(e->b);
      if (ix->k != EK::Num || !ix->isIntLit || ix->num >= (double)ot->elems.size())
        return errT(e->b->loc, "tuple index must be an integer literal in range");
      ix->type = T.number();
      e->field = (int)ix->num;
      e->cls = tupleRecord(ot);
      return ot->elems[e->field];
    }
    case TT::Map:
      return errT(e->loc, "use map.get(key) instead of indexing a Map");
    case TT::Unknown:
      check(e->b, nullptr);
      return T.unknown();
    default:
      return errT(e->loc, "type '" + typeName(ot) + "' cannot be indexed");
  }
}

// The type `e as const` gives: a literal type for a constant, a readonly tuple for an array literal and
// an object type with readonly properties for an object literal, of those recursively (null: e is
// something else)
Type* Checker::asConstType(Expr* e) {
  e = strip(e);
  if (e->k == EK::Str || e->k == EK::Bool || (e->k == EK::Template && e->list.empty())) return constLit(e);
  if (e->k == EK::Num || (e->k == EK::Unary && e->raw == "-" && strip(e->a)->k == EK::Num)) {
    if (e->k == EK::Num) return T.numLit({e->num});
    return T.numLit({-strip(e->a)->num});
  }
  if (e->k == EK::Array) {
    std::vector<Type*> es;
    for (Expr* x : e->list) {
      if (x->k == EK::Spread) return nullptr;
      Type* t = asConstType(x);
      if (!t) return nullptr;
      es.push_back(t);
    }
    if (es.empty()) return nullptr;
    return T.tuple(es, true);
  }
  if (e->k == EK::Object) {
    std::vector<ObjShape::Prop> props;
    for (auto& p : e->props) {
      if (p.spread || p.method || p.accessor || p.shorthand) return nullptr;
      Type* t = asConstType(p.value);
      if (!t) return nullptr;
      props.push_back({p.key, t, false, false, true});
    }
    return T.anonObject(props);
  }
  return nullptr;
}

Type* Checker::checkAs(Expr* e) {
  if (e->asType && e->asType->k == TNK::Ref && e->asType->name == "const" && e->asType->args.empty() && !lookup("const")) {
    Type* t = asConstType(e->a);
    if (!t) return errT(e->loc, "'as const' is supported for literals and for arrays and objects of literals");
    check(e->a, t);
    coerce(e->a, t, "as const");
    return t;
  }
  Type* target = resolveType(e->asType);
  Expr* inner = strip(e->a);
  if (inner->k == EK::Call && inner->a->k == EK::Member && strip(inner->a->a)->k == EK::Ident &&
      strip(inner->a->a)->raw == "JSON" && inner->a->raw == "parse") {
    // JSON.parse(text) as T : typed, validating parse
    if (inner->list.size() != 1) return errT(e->loc, "JSON.parse takes exactly one argument");
    check(inner->list[0], T.string());
    coerce(inner->list[0], T.string());
    inner->type = target;
    e->intrinsic = IN_JSON_PARSE;
    return target;
  }
  // tryParse(text) as T | null (e7:json): the same typed parse, null instead of an error
  auto isTryParse = [&](Expr* c) {
    if (c->k != EK::Call) return false;
    Expr* f = strip(c->a);
    if (f->k == EK::Ident) {
      Sym* s = lookup(f->raw);
      return s && s->k == SymK::Builtin && static_cast<BuiltinSym*>(s)->id == "e7:json.tryParse";
    }
    if (f->k == EK::Member && f->raw == "tryParse" && strip(f->a)->k == EK::Ident) {
      Sym* s = lookup(strip(f->a)->raw);
      return s && s->k == SymK::Namespace && static_cast<NamespaceSym*>(s)->builtin &&
             static_cast<NamespaceSym*>(s)->module == "e7:json";
    }
    return false;
  };
  if (isTryParse(inner)) {
    if (e->raw == "satisfies")
      return errT(e->loc, "tryParse must be used with a type assertion: tryParse(text) as MyType | null");
    if (inner->list.size() != 1) return errT(e->loc, "tryParse takes exactly one argument");
    if (inner->list[0]->k == EK::Spread) return errT(inner->list[0]->loc, "spread is not supported here");
    check(inner->list[0], T.string());
    if (!coerce(inner->list[0], T.string())) return T.unknown();
    if (target->t == TT::Null || target->t == TT::Undefined)
      return errT(e->loc, "tryParse needs a type to parse into: tryParse(text) as MyType | null");
    if (target->t != TT::Optional || !target->hasNull)
      return errT(e->loc, "tryParse returns null when the text is not valid JSON of the expected type: write tryParse(text) as " +
                              typeName(target) + " | null");
    inner->type = target;
    e->intrinsic = IN_JSON_TRY_PARSE;
    return target;
  }
  Type* t = check(e->a, target);
  if (e->raw == "satisfies") {
    coerce(e->a, target);
    return t;
  }
  if (t == target) return t;
  if (assignable(t, target)) { coerce(e->a, target); return target; }
  // downcast class -> subclass (checked), nullable -> non-null
  Type* tb = baseOf(t);
  if (tb->t == TT::Class && target->t == TT::Class && assignable(target, tb)) {
    e->cls = target->cls;
    e->intrinsic = 1;  // checked downcast
    return target;
  }
  if ((tb->t == TT::Object) && target->t == TT::Class) {
    e->cls = target->cls;
    e->intrinsic = 1;
    return target;
  }
  if (t->t == TT::Optional && t->elem == target) {
    e->intrinsic = 2;  // non-null assertion
    return target;
  }
  return errT(e->loc, "conversion of type '" + typeName(t) + "' to type '" + typeName(target) + "' is not supported");
}

}  // namespace e7
