// Checker: builtin namespaces (Math, JSON, ...), global builtin functions, `new`.
#include <cmath>

#include "checker.h"
#include "intrinsics.h"

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
    {"PI", M_PI}, {"E", M_E}, {"LN2", M_LN2}, {"LN10", M_LN10}, {"LOG2E", M_LOG2E}, {"LOG10E", M_LOG10E},
    {"SQRT2", M_SQRT2}, {"SQRT1_2", M_SQRT1_2}};
static const struct { const char* name; double v; } kNumberConsts[] = {
    {"MAX_SAFE_INTEGER", 9007199254740991.0}, {"MIN_SAFE_INTEGER", -9007199254740991.0},
    {"EPSILON", 2.220446049250313e-16}, {"MAX_VALUE", 1.7976931348623157e308}, {"MIN_VALUE", 5e-324},
    {"POSITIVE_INFINITY", INFINITY}, {"NEGATIVE_INFINITY", -INFINITY}, {"NaN", NAN}};

static const char* kMath1[] = {"abs", "ceil", "floor", "round", "trunc", "sign", "sqrt", "cbrt", "exp", "expm1", "log",
                               "log1p", "log2", "log10", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh",
                               "tanh", "asinh", "acosh", "atanh", "fround", "clz32"};
static const char* kMath2[] = {"atan2", "pow", "imul"};

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
    return errT(l, "'" + ns->name + "." + name + "' is not supported" + (isCall ? "" : " as a value"));
  };
  if (!ns->builtin) {
    // file module namespace
    auto it = ns->members.find(name);
    if (it == ns->members.end()) return errT(l, "module '" + ns->name + "' has no exported member '" + name + "'");
    Sym* s = it->second;
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
          if (st != T.array(num)) return errT(a->loc, "spread argument must be number[]");
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
      if (args.size() == 1) args.push_back(N(0));
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
        if (t->t == TT::Void) return errT(a->loc, "cannot print a void value");
      }
      e->intrinsic = IN_CONSOLE_LOG;
      e->raw = (name == "warn" || name == "error") ? "error" : "log";
      return T.voidT();
    }
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
    if (isCall && name == "freeze") {
      if (args.size() != 1) return errT(l, "Object.freeze expects one argument");
      Type* t = check(args[0], nullptr);
      e->intrinsic = IN_NUMBER;  // identity
      e->raw = "identity";
      return t;
    }
    return bad();
  }
  if (M == "Array") {
    if (isCall && name == "from") {
      if (args.size() == 2) {
        // Array.from({ length: n }, (_, i) => ...)
        Expr* o = strip(args[0]);
        if (o->k != EK::Object || o->props.size() != 1 || o->props[0].key != "length")
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
      Type* et = elemTypeForIter(args[0], t, args[0]->loc);
      if (t->t == TT::Map) e->cls = tupleRecord(et);
      e->intrinsic = IN_ARRAY_FROM;
      return T.array(et);
    }
    if (isCall && name == "of") {
      Expr* arr = P.arena.expr(EK::Array, l);
      arr->list = args;
      Type* t = check(arr, expected);
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
    return errT(l, "Date is not supported except Date.now()");
  }
  if (M == "e7:crypto" || M == "e7:encoding") {
    BuiltinSym tmp;
    std::string id = M + "." + name;
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
    if (isIntLiteralExpr(args[0])) t = check(args[0], target);
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
      if (t1->t != TT::Int && t1->t != TT::Unknown) return errT(args[1]->loc, id + "() requires an integer argument");
      if ((id == "mulhi64" || id == "imulhi64") && t1 != t0) return errT(args[1]->loc, "operands must have the same type");
    }
    e->intrinsic = IN_INTOP;
    e->raw = id;
    return t0;
  }
  if (id == "parseInt") {
    if (args.size() == 1) args.push_back(N(0));
    if (!argsMatch(e, args, {str, num}, 2, "parseInt")) return T.unknown();
    nat("Global.parseInt");
    return num;
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
  if (id == "Array()" || id == "Map()" || id == "Set()" || id == "Uint8Array()" || id == "Error()")
    return errT(l, "'" + id.substr(0, id.size() - 2) + "' must be called with 'new'");
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
    return errT(l, "module 'e7:encoding' has no function '" + n + "'");
  }
  // ------------------------------------------------------------ e7:crypto
  if (id.rfind("e7:crypto.", 0) == 0) {
    std::string n = id.substr(10);
    static const char* hashes[] = {"md5", "sha1", "sha224", "sha256", "sha384", "sha512", "sha512_256",
                                   "sha3_224", "sha3_256", "sha3_384", "sha3_512", "keccak256"};
    for (const char* h : hashes)
      if (n == h) {
        if (!argsMatch(e, args, {bytes}, 1, h)) return T.unknown();
        nat(std::string("crypto.hash.") + h);
        return bytes;
      }
    Type* handle = T.handle();
    Type* optBytes = T.optional(bytes, true, false);
    Type* optStr = T.optional(str, true, false);
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
    if (n == "crc32") {
      if (!argsMatch(e, args, {bytes}, 1, "crc32")) return T.unknown();
      nat("crypto.crc32");
      return num;
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
    if (n == "aesCbcEncrypt" || n == "aesCbcDecrypt" || n == "aesCtr") {
      if (!argsMatch(e, args, {bytes, bytes, bytes}, 3, n.c_str())) return T.unknown();
      nat("crypto." + n);
      return n == "aesCbcDecrypt" ? T.optional(bytes, true, false) : bytes;
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
    if (n == "ecdsaSign") {
      if (!argsMatch(e, args, {str, str, bytes, bytes}, 4, n.c_str())) return T.unknown();
      nat("crypto.ecdsaSign");
      return bytes;
    }
    if (n == "ecdsaVerify") {
      if (!argsMatch(e, args, {str, str, bytes, bytes, bytes}, 5, n.c_str())) return T.unknown();
      nat("crypto.ecdsaVerify");
      return boo;
    }
    if (n == "ecdsaPublicKey") {
      if (!argsMatch(e, args, {str, bytes}, 2, n.c_str())) return T.unknown();
      nat("crypto.ecdsaPublicKey");
      return bytes;
    }
    if (n == "rsaVerify") {
      if (args.size() == 4) args.push_back(mkNull(P, T, l));
      if (!argsMatch(e, args, {str, bytes, bytes, bytes, optStr}, 5, n.c_str())) return T.unknown();
      nat("crypto.rsaVerify");
      return boo;
    }
    if (n == "rsaSign") {
      if (args.size() == 3) args.push_back(mkNull(P, T, l));
      if (!argsMatch(e, args, {str, bytes, bytes, optStr}, 4, n.c_str())) return T.unknown();
      nat("crypto.rsaSign");
      return bytes;
    }
    return errT(l, "module 'e7:crypto' has no function '" + n + "'");
  }
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
  if (c->k != EK::Ident) return errT(l, "unsupported 'new' expression");
  const std::string& name = c->raw;
  Sym* s = lookup(name);
  if (s && s->k == SymK::Class) {
    ClassSym* cls = static_cast<ClassSym*>(s);
    resolveClass(cls);
    if (!e->typeArgs.empty()) return errT(l, "class '" + name + "' is not generic");
    resolveSig(cls->ctor);
    checkArgs(e, cls->ctor->type, args);
    e->cls = cls;
    e->callee = cls->ctor;
    return cls->type;
  }
  if (s && s->k != SymK::Namespace) return errT(l, "'" + name + "' is not a class");
  auto typeArg = [&](size_t i) -> Type* { return i < e->typeArgs.size() ? resolveType(e->typeArgs[i]) : nullptr; };
  if (name == "Map") {
    Type *kt = typeArg(0), *vt = typeArg(1);
    if (!e->typeArgs.empty() && e->typeArgs.size() != 2) return errT(l, "Map requires two type arguments");
    if (!kt && exp && exp->t == TT::Map) { kt = exp->key; vt = exp->val; }
    if (!args.empty()) {
      if (args.size() != 1) return errT(l, "new Map() takes at most one argument");
      Type* want = kt ? T.array(T.tuple({kt, vt})) : nullptr;
      Type* at = check(args[0], want);
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
      Type* at = check(args[0], et ? T.array(et) : nullptr);
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
    if (!et) return errT(l, "cannot infer Array element type; write new Array<T>(n)");
    if (args.size() > 1) return errT(l, "use an array literal instead of new Array(a, b, ...)");
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
  if (name == "Error" || name == "TypeError" || name == "RangeError") {
    if (args.size() == 1) { check(args[0], T.string()); coerce(args[0], T.string()); }
    else if (!args.empty()) return errT(l, "Error takes at most one argument");
    if (args.empty()) { Expr* sx = P.arena.expr(EK::Str, l); sx->type = T.string(); args.push_back(sx); }
    nat("Error.new");
    return T.error();
  }
  if (name == "Date" || name == "RegExp" || name == "Promise" || name == "WeakMap" || name == "WeakSet" || name == "Proxy")
    return errT(l, "'" + name + "' is not supported in E7");
  return errT(l, "cannot find class '" + name + "'");
}

}  // namespace e7
