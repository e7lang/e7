// Checker: expressions.
#include <cmath>

#include "checker.h"
#include "intrinsics.h"

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
    case EK::Ident: return checkIdent(e);
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
Type* Checker::checkIdent(Expr* e) {
  Sym* s = lookup(e->raw);
  if (!s) return errT(e->loc, "cannot find name '" + e->raw + "'");
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
      if (f->hasDefaults) return errT(e->loc, "functions with default parameters cannot be used as values; wrap it in an arrow function");
      e->callee = f;
      if (f->selfVar) markCapture(f->selfVar);
      return f->type;
    }
    case SymK::Class: return errT(e->loc, "class '" + e->raw + "' cannot be used as a value");
    case SymK::Enum: return errT(e->loc, "enum '" + e->raw + "' cannot be used as a value");
    case SymK::Namespace: return errT(e->loc, "'" + e->raw + "' cannot be used as a value");
    case SymK::Builtin: return errT(e->loc, "builtin function '" + e->raw + "' cannot be used as a value; wrap it in an arrow function");
    default: return errT(e->loc, "'" + e->raw + "' is a type, not a value");
  }
}

// ------------------------------------------------------------------ records
Type* Checker::recordFor(const std::vector<std::pair<std::string, Type*>>& fields, bool tuple) {
  // find identical layout (names + value types)
  for (ClassSym* c : recordClasses) {
    if (c->fields.size() != fields.size() || c->isTuple != tuple) continue;
    bool same = true;
    for (size_t i = 0; i < fields.size() && same; i++)
      if (c->fields[i].name != fields[i].first || vtOf(c->fields[i].type) != vtOf(fields[i].second)) same = false;
    if (same) return c->type;
  }
  ClassSym* c = P.make<ClassSym>();
  c->name = "{";
  for (size_t i = 0; i < fields.size(); i++) c->name += (i ? "," : "") + fields[i].first;
  c->name += "}";
  c->isRecord = true;
  c->isTuple = tuple;
  c->state = 2;
  for (auto& f : fields) {
    c->fieldIdx[f.first] = (int)c->fields.size();
    c->fields.push_back({f.first, f.second, nullptr, false, c, Loc()});
  }
  c->type = T.classType(c);
  c->classIdx = (int)P.classes.size();
  P.classes.push_back(c);
  recordClasses.push_back(c);
  return c->type;
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
    case TT::Unknown: return T.unknown();
    default: return errT(l, "type '" + typeName(t) + "' is not iterable");
  }
}

Type* Checker::checkArray(Expr* e, Type* expected) {
  Type* exp = expected ? baseOf(expected) : nullptr;
  if (exp && exp->t == TT::Tuple) {
    if (e->list.size() != exp->elems.size())
      return errT(e->loc, "expected a tuple of " + std::to_string(exp->elems.size()) + " elements");
    for (size_t i = 0; i < e->list.size(); i++) {
      if (e->list[i]->k == EK::Spread) return errT(e->loc, "spread is not allowed in tuple literals");
      check(e->list[i], exp->elems[i]);
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
  for (Expr*& x : e->list) {
    Type* t;
    if (x->k == EK::Spread) {
      Type* st = check(x->a, nullptr);
      x->type = st;
      if (st->t == TT::Array || st->t == TT::Set) t = st->elem;
      else if (st->t == TT::String) t = T.string();
      else if (st->t == TT::Bytes) t = T.number();
      else if (st->t == TT::Map) t = T.tuple({st->key, st->val});
      else { err(x->loc, "type '" + typeName(st) + "' cannot be spread into an array"); t = T.unknown(); }
      if (elemExp && t != elemExp && !(t->t == TT::Unknown) && !(kindOf(t) == K_R && kindOf(elemExp) == K_R && assignable(t, elemExp)))
        err(x->loc, "spread element type '" + typeName(t) + "' is not assignable to '" + typeName(elemExp) + "'");
    } else {
      t = check(x, elemExp);
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

Type* Checker::checkObject(Expr* e, Type* expected) {
  Type* exp = expected ? baseOf(expected) : nullptr;
  for (auto& p : e->props)
    if (p.spread) return errT(p.loc, "object spread is not supported");
  for (size_t i = 0; i < e->props.size(); i++)
    for (size_t j = 0; j < i; j++)
      if (e->props[i].key == e->props[j].key) return errT(e->props[i].loc, "duplicate property '" + e->props[i].key + "'");
  if (exp && exp->t == TT::Object) {
    ObjShape* sh = exp->shape;
    if (!sh->methods.empty())
      return errT(e->loc, "object literals cannot implement '" + typeName(exp) + "' (it has methods); use a class");
    std::vector<std::pair<std::string, Type*>> fields;
    for (auto& p : e->props) {
      const ObjShape::Prop* sp = sh->findProp(p.key);
      if (!sp) { err(p.loc, "object literal may only specify known properties; '" + p.key + "' does not exist in '" + typeName(exp) + "'"); continue; }
      check(p.value, sp->type);
      coerce(p.value, sp->type);
      fields.push_back({p.key, sp->type});
    }
    for (auto& sp : sh->props) {
      bool present = false;
      for (auto& p : e->props) if (p.key == sp.name) present = true;
      if (present) continue;
      if (!sp.optional && !(sp.type->t == TT::Optional && sp.type->hasUndef))
        err(e->loc, "property '" + sp.name + "' is missing in object literal of type '" + typeName(exp) + "'");
      fields.push_back({sp.name, sp.type});
    }
    e->cls = recordFor(fields)->cls;
    return exp;
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
      err(x->loc, "cannot find name '" + x->raw + "'");
      return false;
    }
    case EK::Member:
    case EK::Index:
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

Type* Checker::checkUpdate(Expr* e) {
  if (!checkLValue(e->a)) return T.unknown();
  Type* t = check(e->a, nullptr);
  Type* st = storageTypeOf(e->a);
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
  if (op == "instanceof") {
    Type* lt = check(e->a, nullptr);
    Expr* r = strip(e->b);
    if (r->k != EK::Ident) return errT(e->b->loc, "right side of 'instanceof' must be a class name");
    Sym* s = lookup(r->raw);
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
    return mismatch();
  }
  if (op == "===" || op == "!==" || op == "==" || op == "!=") {
    bool loose = op == "==" || op == "!=";
    bool lNull = isNullish(lt), rNull = isNullish(rt);
    if (loose && !lNull && !rNull) return errT(e->loc, "use '" + std::string(op == "==" ? "===" : "!==") + "' instead of '" + op + "'");
    if (lNull || rNull) {
      Type* other = lNull ? rt : lt;
      if (isNullish(other)) return T.boolean();
      if (other->t != TT::Optional) {
        return errT(e->loc, "this comparison is always " + std::string(op[0] == '=' ? "false" : "true") + ": '" +
                                typeName(other) + "' cannot be null or undefined");
      }
      return T.boolean();
    }
    Type* lb = baseOf(lt);
    Type* rb = baseOf(rt);
    if (lb == rb) return T.boolean();
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
    if (lt->t != TT::Optional && !isNullish(lt)) {
      // allowed but pointless; result is left type
      return lt;
    }
    if (isNullish(lt)) return rt;
    Type* res;
    if (rt->t == TT::Optional || isNullish(rt)) res = unify(lt, rt, e->loc, "'??' expression");
    else res = unify(lb, rt, e->loc, "'??' expression");
    if (rt != res) coerce(e->b, res);
    return res;
  }
  return errT(e->loc, "unsupported operator '" + op + "'");
}

Type* Checker::checkAssign(Expr* e) {
  const std::string& op = e->raw;
  Expr* target = strip(e->a);
  if (target->k == EK::Array) {
    if (op != "=") return errT(e->loc, "invalid destructuring assignment");
    std::vector<Type*> tts;
    for (Expr*& x : target->list) {
      if (x->k == EK::Spread) return errT(x->loc, "rest elements are not supported");
      if (!checkLValue(x)) return T.unknown();
      Type* t = check(x, nullptr);
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
    lt = check(e->a, nullptr);
    if (target->k == EK::Member && target->field >= 0 && target->cls) lt = target->cls->fields[target->field].type;
    else if (target->k == EK::Member && target->field < 0 && target->intrinsic == 0 && target->a->type &&
             baseOf(target->a->type)->t == TT::Object) {
      const ObjShape::Prop* p = baseOf(target->a->type)->shape->findProp(target->raw);
      if (p) lt = p->type;
    }
    if (target->k == EK::Member && target->intrinsic == IN_ARR_LEN) {
      target->intrinsic = IN_ARR_LENGTH_SET;
    } else if (target->k == EK::Member && (target->intrinsic != 0 || !target->native.empty())) {
      return errT(e->loc, "cannot assign to '" + target->raw + "'");
    }
    if (target->k == EK::Index && target->a->type && baseOf(target->a->type)->t == TT::String)
      return errT(e->loc, "strings are immutable");
    e->a->type = lt;
  }
  std::string key = pathKey(e->a);
  if (op == "=") {
    Type* rt = check(e->b, lt);
    coerce(e->b, lt);
    invalidatePath(key);
    if (lt->t == TT::Optional && rt->t != TT::Optional && !isNullish(rt) && rt->t != TT::Unknown) setNarrow(key, rt == lt->elem ? lt->elem : baseOf(lt));
    return lt;
  }
  if (op == "??=" || op == "||=" || op == "&&=") {
    if (op != "??=") return errT(e->loc, "'" + op + "' is not supported");
    if (lt->t != TT::Optional) return errT(e->loc, "'??=' requires a nullable target");
    check(e->b, lt->elem);
    coerce(e->b, lt);
    invalidatePath(key);
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

Type* Checker::checkCondExpr(Expr* e, Type* expected) {
  checkCond(e->a);
  std::vector<Fact> tf, ff;
  factsFrom(e->a, true, tf);
  factsFrom(e->a, false, ff);
  pushScope();
  applyFacts(tf);
  Type* bt = check(e->b, expected);
  popScope();
  pushScope();
  applyFacts(ff);
  Type* ct = check(e->c, expected);
  popScope();
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
  if (c->k == EK::Binary && c->raw == "instanceof") {
    if (!whenTrue || !c->cls) return;
    std::string key = pathKey(c->a);
    if (!key.empty()) out.push_back({key, c->cls->type});
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
    f->arrow = e;
    f->parent = curFn;
    f->defScope = scope;
    f->isArrowThis = e->intrinsic != IN_FUNC_EXPR;
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
    if (p.type) t = resolveType(p.type);
    else if (exp && i < exp->params.size()) t = exp->params[i];
    else if (p.init) t = check(p.init, nullptr);
    else { err(p.loc, "parameter '" + p.name + "' implicitly has an unknown type; add a type annotation"); t = T.unknown(); }
    if (p.optional && p.type) t = T.optional(t, false, true);
    if (p.rest) rest = true;
    if (p.init) {
      err(p.loc, "default parameter values are not supported in arrow functions");
    }
    ps.push_back(t);
    opt.push_back(p.optional);
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
  else if (exp && exp->ret->t == TT::Void) ret = T.voidT();
  else if (exp && exp->ret->t != TT::Unknown) ret = exp->ret;
  f->sigResolved = true;
  f->inferRet = ret == nullptr;
  f->type = T.func(ps, ret ? ret : T.unknown(), opt, rest);
  // a 'function' expression with no explicit this: no this binding
  checkFunction(f);
  // if the expected type has fewer params than the arrow uses, error above; if more, codegen adapts
  return f->type;
}

// ------------------------------------------------------------------ member access
Type* Checker::checkMember(Expr* e, Type* expected) {
  Expr* obj = strip(e->a);
  const std::string& name = e->raw;
  // static / namespace access
  if (obj->k == EK::Ident) {
    Sym* s = lookup(obj->raw);
    if (s && s->k == SymK::Namespace) return checkNamespaceMember(e, static_cast<NamespaceSym*>(s), name, false, expected);
    if (s && s->k == SymK::Enum) {
      EnumSym* en = static_cast<EnumSym*>(s);
      resolveEnum(en);
      for (auto& m : en->members)
        if (m.name == name) {
          if (en->isString) { e->intrinsic = IN_CONST_STR; e->str = m.str; }
          else { e->intrinsic = IN_CONST_NUM; e->num = m.num; }
          return en->type;
        }
      return errT(e->loc, "enum '" + en->name + "' has no member '" + name + "'");
    }
    if (s && s->k == SymK::Class) {
      ClassSym* c = static_cast<ClassSym*>(s);
      resolveClass(c);
      auto it = c->staticFields.find(name);
      if (it != c->staticFields.end()) {
        e->intrinsic = IN_STATIC_FIELD;
        e->var = it->second;
        if (!it->second->type) globalVarType(it->second);
        return it->second->type ? it->second->type : T.unknown();
      }
      if (c->staticMethods.count(name)) return errT(e->loc, "static methods cannot be used as values; wrap in an arrow function");
      return errT(e->loc, "class '" + c->name + "' has no static member '" + name + "'");
    }
  }
  Type* ot = check(e->a, nullptr);
  if (ot->t == TT::Unknown) return T.unknown();
  Type* bt = ot;
  if (ot->t == TT::Optional) {
    if (!e->optional) return errT(e->loc, "object is possibly '" + std::string(ot->hasNull ? "null" : "undefined") + "'; use '?.' or a check");
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
        e->field = it->second;
        e->cls = c;
        result = c->fields[it->second].type;
        break;
      }
      if (c->methodIdx.count(name)) return errT(e->loc, "method '" + name + "' cannot be used as a value; use an arrow function");
      return errT(e->loc, "property '" + name + "' does not exist on type '" + c->name + "'");
    }
    case TT::Object: {
      const ObjShape::Prop* p = bt->shape->findProp(name);
      if (!p) {
        if (bt->shape->findMethod(name)) return errT(e->loc, "method '" + name + "' cannot be used as a value");
        return errT(e->loc, "property '" + name + "' does not exist on type '" + typeName(bt) + "'");
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
  if (ot->t == TT::Optional) {
    if (!e->optional) return errT(e->loc, "object is possibly null or undefined");
    return errT(e->loc, "optional element access is not supported");
  }
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

Type* Checker::checkAs(Expr* e) {
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
