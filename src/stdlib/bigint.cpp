// Arbitrary-precision integers (JavaScript BigInt semantics).
#include <algorithm>

#include "util.h"

namespace e7 {

using Mag = std::vector<uint32_t>;

static void trim(Mag& m) {
  while (!m.empty() && m.back() == 0) m.pop_back();
}
static int cmpMag(const Mag& a, const Mag& b) {
  if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
  for (size_t i = a.size(); i-- > 0;)
    if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  return 0;
}
static Mag addMag(const Mag& a, const Mag& b) {
  Mag r(std::max(a.size(), b.size()) + 1, 0);
  uint64_t c = 0;
  for (size_t i = 0; i < r.size(); i++) {
    uint64_t s = c + (i < a.size() ? a[i] : 0) + (i < b.size() ? b[i] : 0);
    r[i] = (uint32_t)s;
    c = s >> 32;
  }
  trim(r);
  return r;
}
static Mag subMag(const Mag& a, const Mag& b) {  // a >= b
  Mag r(a.size(), 0);
  int64_t br = 0;
  for (size_t i = 0; i < a.size(); i++) {
    int64_t d = (int64_t)a[i] - (i < b.size() ? b[i] : 0) - br;
    br = d < 0;
    if (d < 0) d += (int64_t)1 << 32;
    r[i] = (uint32_t)d;
  }
  trim(r);
  return r;
}
static Mag mulMag(const Mag& a, const Mag& b) {
  if (a.empty() || b.empty()) return {};
  Mag r(a.size() + b.size(), 0);
  for (size_t i = 0; i < a.size(); i++) {
    uint64_t c = 0;
    for (size_t j = 0; j < b.size(); j++) {
      uint64_t t = (uint64_t)a[i] * b[j] + r[i + j] + c;
      r[i + j] = (uint32_t)t;
      c = t >> 32;
    }
    size_t k = i + b.size();
    while (c) {
      uint64_t t = (uint64_t)r[k] + c;
      r[k++] = (uint32_t)t;
      c = t >> 32;
    }
  }
  trim(r);
  return r;
}
static uint32_t divSmall(Mag& a, uint32_t d) {  // in place, returns remainder
  uint64_t rem = 0;
  for (size_t i = a.size(); i-- > 0;) {
    uint64_t cur = (rem << 32) | a[i];
    a[i] = (uint32_t)(cur / d);
    rem = cur % d;
  }
  trim(a);
  return (uint32_t)rem;
}
// Knuth algorithm D
static void divMod(const Mag& a, const Mag& b, Mag& q, Mag& r) {
  if (cmpMag(a, b) < 0) { q.clear(); r = a; return; }
  if (b.size() == 1) {
    q = a;
    uint32_t rem = divSmall(q, b[0]);
    r.clear();
    if (rem) r.push_back(rem);
    return;
  }
  int s = 0;
  uint32_t top = b.back();
  while (!(top & 0x80000000u)) { top <<= 1; s++; }
  Mag vn(b.size()), un(a.size() + 1);
  for (size_t i = b.size(); i-- > 1;) vn[i] = (b[i] << s) | (s ? (uint32_t)((uint64_t)b[i - 1] >> (32 - s)) : 0);
  vn[0] = b[0] << s;
  un[a.size()] = s ? (uint32_t)((uint64_t)a.back() >> (32 - s)) : 0;
  for (size_t i = a.size(); i-- > 1;) un[i] = (a[i] << s) | (s ? (uint32_t)((uint64_t)a[i - 1] >> (32 - s)) : 0);
  un[0] = a[0] << s;
  size_t n = b.size(), m = a.size() - b.size();
  q.assign(m + 1, 0);
  for (size_t j = m + 1; j-- > 0;) {
    uint64_t num = ((uint64_t)un[j + n] << 32) | un[j + n - 1];
    uint64_t qhat = num / vn[n - 1];
    uint64_t rhat = num % vn[n - 1];
    while (qhat >= ((uint64_t)1 << 32) || qhat * vn[n - 2] > ((rhat << 32) | un[j + n - 2])) {
      qhat--;
      rhat += vn[n - 1];
      if (rhat >= ((uint64_t)1 << 32)) break;
    }
    int64_t borrow = 0;
    uint64_t carry = 0;
    for (size_t i = 0; i < n; i++) {
      uint64_t p = qhat * vn[i] + carry;
      carry = p >> 32;
      int64_t t = (int64_t)un[i + j] - (int64_t)(uint32_t)p - borrow;
      borrow = t < 0;
      un[i + j] = (uint32_t)t;
    }
    int64_t t = (int64_t)un[j + n] - (int64_t)carry - borrow;
    un[j + n] = (uint32_t)t;
    if (t < 0) {
      qhat--;
      uint64_t c = 0;
      for (size_t i = 0; i < n; i++) {
        uint64_t s2 = (uint64_t)un[i + j] + vn[i] + c;
        un[i + j] = (uint32_t)s2;
        c = s2 >> 32;
      }
      un[j + n] += (uint32_t)c;
    }
    q[j] = (uint32_t)qhat;
  }
  trim(q);
  r.assign(n, 0);
  for (size_t i = 0; i < n; i++) r[i] = (un[i] >> s) | (s ? (uint32_t)((uint64_t)un[i + 1] << (32 - s)) : 0);
  trim(r);
}

static BigObj* mk(VM& vm, Mag m, bool neg) {
  trim(m);
  BigObj* b = vm.newBig();
  b->mag = std::move(m);
  b->neg = neg && !b->mag.empty();
  if (b->mag.size() > (1u << 20)) vm.error("RangeError: Maximum BigInt size exceeded");
  return b;
}

BigObj* bigFromU64(VM& vm, uint64_t v, bool neg) {
  Mag m;
  if (v) m.push_back((uint32_t)v);
  if (v >> 32) m.push_back((uint32_t)(v >> 32));
  return mk(vm, m, neg);
}

int bigCmp(const BigObj* a, const BigObj* b) {
  if (a->neg != b->neg) return a->neg ? -1 : 1;
  int c = cmpMag(a->mag, b->mag);
  return a->neg ? -c : c;
}

uint64_t bigToU64(const BigObj* b) {
  uint64_t v = 0;
  if (!b->mag.empty()) v = b->mag[0];
  if (b->mag.size() > 1) v |= (uint64_t)b->mag[1] << 32;
  return b->neg ? 0 - v : v;
}

std::string bigToString(const BigObj* b, int radix) {
  if (b->mag.empty()) return "0";
  Mag m = b->mag;
  std::string out;
  // chunked conversion
  uint32_t chunk = radix;
  int digits = 1;
  while ((uint64_t)chunk * radix <= 0xFFFFFFFFu) { chunk *= radix; digits++; }
  while (!m.empty()) {
    uint32_t rem = divSmall(m, chunk);
    for (int i = 0; i < digits; i++) {
      int d = rem % radix;
      rem /= radix;
      out.push_back((char)(d < 10 ? '0' + d : 'a' + d - 10));
      if (m.empty() && rem == 0) break;
    }
  }
  while (out.size() > 1 && out.back() == '0') out.pop_back();
  if (b->neg) out.push_back('-');
  std::reverse(out.begin(), out.end());
  return out;
}

double bigToDouble(const BigObj* b) {
  std::string s = bigToString(b, 10);
  return strtod(s.c_str(), nullptr);  // correctly rounded
}

static Mag mulSmallAdd(const Mag& m, uint32_t mul, uint32_t add) {
  Mag r(m.size() + 1, 0);
  uint64_t c = add;
  for (size_t i = 0; i < m.size(); i++) {
    uint64_t t = (uint64_t)m[i] * mul + c;
    r[i] = (uint32_t)t;
    c = t >> 32;
  }
  r[m.size()] = (uint32_t)c;
  trim(r);
  return r;
}

// literal: decimal or 0x/0o/0b; string (BigInt("...")): trimmed, optional sign for decimal
BigObj* bigParse(VM& vm, const std::string& in, bool literal) {
  std::string s = in;
  size_t b = 0, e = s.size();
  if (!literal) {
    while (b < e && isspace((unsigned char)s[b])) b++;
    while (e > b && isspace((unsigned char)s[e - 1])) e--;
  }
  s = s.substr(b, e - b);
  bool neg = false;
  int radix = 10;
  size_t i = 0;
  if (s.empty()) return literal ? nullptr : mk(vm, {}, false);
  if (s.size() > 2 && s[0] == '0' && strchr("xXoObB", s[1])) {
    char c = (char)tolower(s[1]);
    radix = c == 'x' ? 16 : c == 'o' ? 8 : 2;
    i = 2;
  } else if (!literal && (s[0] == '+' || s[0] == '-')) {
    neg = s[0] == '-';
    i = 1;
  }
  if (i >= s.size()) return nullptr;
  Mag m;
  for (; i < s.size(); i++) {
    char c = s[i];
    int d = isdigit((unsigned char)c) ? c - '0' : isalpha((unsigned char)c) ? tolower(c) - 'a' + 10 : 99;
    if (d >= radix) return nullptr;
    m = mulSmallAdd(m, (uint32_t)radix, (uint32_t)d);
  }
  return mk(vm, m, neg);
}

// two's complement helpers for bitwise ops: represent as (mag, neg) -> infinite two's complement limbs
static Mag toTwos(const BigObj* b, size_t n) {
  Mag r(n, 0);
  for (size_t i = 0; i < b->mag.size() && i < n; i++) r[i] = b->mag[i];
  if (b->neg) {
    uint64_t c = 1;
    for (size_t i = 0; i < n; i++) {
      uint64_t t = (uint64_t)(uint32_t)~r[i] + c;
      r[i] = (uint32_t)t;
      c = t >> 32;
    }
  }
  return r;
}
static BigObj* fromTwos(VM& vm, Mag r) {
  bool neg = !r.empty() && (r.back() & 0x80000000u);
  if (neg) {
    uint64_t c = 1;
    for (auto& x : r) {
      uint64_t t = (uint64_t)(uint32_t)~x + c;
      x = (uint32_t)t;
      c = t >> 32;
    }
  }
  return mk(vm, r, neg);
}

static BigObj* addSigned(VM& vm, const BigObj* a, const BigObj* b, bool negateB) {
  bool bn = negateB ? !b->neg : b->neg;
  if (b->mag.empty()) bn = false;
  if (a->neg == bn) return mk(vm, addMag(a->mag, b->mag), a->neg);
  int c = cmpMag(a->mag, b->mag);
  if (c == 0) return mk(vm, {}, false);
  if (c > 0) return mk(vm, subMag(a->mag, b->mag), a->neg);
  return mk(vm, subMag(b->mag, a->mag), bn);
}

E7_NATIVE(bi_add) { E7_UNUSED; r->r = addSigned(vm, Big(vm, a[0]), Big(vm, a[1]), false); }
E7_NATIVE(bi_sub) { E7_UNUSED; r->r = addSigned(vm, Big(vm, a[0]), Big(vm, a[1]), true); }
E7_NATIVE(bi_mul) {
  E7_UNUSED;
  BigObj *x = Big(vm, a[0]), *y = Big(vm, a[1]);
  if (x->mag.size() + y->mag.size() > (1u << 20)) vm.error("RangeError: Maximum BigInt size exceeded");
  r->r = mk(vm, mulMag(x->mag, y->mag), x->neg != y->neg);
}
E7_NATIVE(bi_div) {
  E7_UNUSED;
  BigObj *x = Big(vm, a[0]), *y = Big(vm, a[1]);
  if (y->mag.empty()) vm.error("RangeError: Division by zero");
  Mag q, rem;
  divMod(x->mag, y->mag, q, rem);
  r->r = mk(vm, q, x->neg != y->neg);
}
E7_NATIVE(bi_mod) {
  E7_UNUSED;
  BigObj *x = Big(vm, a[0]), *y = Big(vm, a[1]);
  if (y->mag.empty()) vm.error("RangeError: Division by zero");
  Mag q, rem;
  divMod(x->mag, y->mag, q, rem);
  r->r = mk(vm, rem, x->neg);
}
E7_NATIVE(bi_pow) {
  E7_UNUSED;
  BigObj *x = Big(vm, a[0]), *y = Big(vm, a[1]);
  if (y->neg) vm.error("RangeError: Exponent must be non-negative");
  if (y->mag.size() > 1) {
    if (x->mag.empty()) { r->r = mk(vm, {}, false); return; }
    if (x->mag.size() == 1 && x->mag[0] == 1) { r->r = mk(vm, {1}, x->neg && (y->mag[0] & 1)); return; }
    vm.error("RangeError: Maximum BigInt size exceeded");
  }
  uint32_t e = y->mag.empty() ? 0 : y->mag[0];
  Mag result{1}, base = x->mag;
  if ((uint64_t)x->mag.size() * e > (1u << 20) && x->mag.size() > 0 && !(x->mag.size() == 1 && x->mag[0] <= 1))
    vm.error("RangeError: Maximum BigInt size exceeded");
  uint32_t ee = e;
  while (ee) {
    if (ee & 1) result = mulMag(result, base);
    ee >>= 1;
    if (ee) base = mulMag(base, base);
  }
  r->r = mk(vm, result, x->neg && (e & 1));
}
E7_NATIVE(bi_neg) {
  E7_UNUSED;
  BigObj* x = Big(vm, a[0]);
  r->r = mk(vm, x->mag, !x->neg);
}
E7_NATIVE(bi_not) {
  E7_UNUSED;
  // ~x = -x - 1
  BigObj* x = Big(vm, a[0]);
  BigObj* one = mk(vm, {1}, false);
  BigObj* nx = mk(vm, x->mag, !x->neg);
  r->r = addSigned(vm, nx, one, true);
}
static void bitwise(VM& vm, Slot* a, Slot* r, int op) {
  BigObj *x = Big(vm, a[0]), *y = Big(vm, a[1]);
  size_t n = std::max(x->mag.size(), y->mag.size()) + 1;
  Mag tx = toTwos(x, n), ty = toTwos(y, n), res(n);
  for (size_t i = 0; i < n; i++) res[i] = op == 0 ? (tx[i] & ty[i]) : op == 1 ? (tx[i] | ty[i]) : (tx[i] ^ ty[i]);
  r->r = fromTwos(vm, res);
}
E7_NATIVE(bi_and) { E7_UNUSED; bitwise(vm, a, r, 0); }
E7_NATIVE(bi_or) { E7_UNUSED; bitwise(vm, a, r, 1); }
E7_NATIVE(bi_xor) { E7_UNUSED; bitwise(vm, a, r, 2); }
static BigObj* shiftLeft(VM& vm, const BigObj* x, uint64_t s) {
  if (x->mag.empty()) return mk(vm, {}, false);
  if (s > (1u << 25)) vm.error("RangeError: Maximum BigInt size exceeded");
  size_t words = (size_t)(s / 32);
  int bits = (int)(s % 32);
  Mag r(words, 0);
  uint32_t carry = 0;
  for (uint32_t w : x->mag) {
    r.push_back((w << bits) | carry);
    carry = bits ? (uint32_t)((uint64_t)w >> (32 - bits)) : 0;
  }
  if (carry) r.push_back(carry);
  return mk(vm, r, x->neg);
}
static BigObj* shiftRight(VM& vm, const BigObj* x, uint64_t s) {
  // floor division by 2^s
  size_t words = (size_t)(s / 32);
  int bits = (int)(s % 32);
  if (words >= x->mag.size()) return x->neg ? mk(vm, {1}, true) : mk(vm, {}, false);
  Mag r;
  bool lost = false;
  for (size_t i = 0; i < words; i++) if (x->mag[i]) lost = true;
  if (bits && (x->mag[words] & ((1u << bits) - 1))) lost = true;
  for (size_t i = words; i < x->mag.size(); i++) {
    uint32_t lo = x->mag[i] >> bits;
    uint32_t hi = (bits && i + 1 < x->mag.size()) ? (uint32_t)((uint64_t)x->mag[i + 1] << (32 - bits)) : 0;
    r.push_back(lo | hi);
  }
  trim(r);
  if (x->neg && lost) r = addMag(r, {1});
  return mk(vm, r, x->neg);
}
static uint64_t shiftAmount(VM& vm, const BigObj* y, bool& negative) {
  negative = y->neg;
  if (y->mag.size() > 2 || (y->mag.size() == 2 && y->mag[1])) {
    if (!y->neg) vm.error("RangeError: Maximum BigInt size exceeded");
    return UINT64_MAX;
  }
  return y->mag.empty() ? 0 : y->mag[0] | (y->mag.size() > 1 ? (uint64_t)y->mag[1] << 32 : 0);
}
E7_NATIVE(bi_shl) {
  E7_UNUSED;
  BigObj *x = Big(vm, a[0]), *y = Big(vm, a[1]);
  bool neg;
  uint64_t s = shiftAmount(vm, y, neg);
  r->r = neg ? shiftRight(vm, x, s) : shiftLeft(vm, x, s);
}
E7_NATIVE(bi_shr) {
  E7_UNUSED;
  BigObj *x = Big(vm, a[0]), *y = Big(vm, a[1]);
  bool neg;
  uint64_t s = shiftAmount(vm, y, neg);
  r->r = neg ? shiftLeft(vm, x, s) : shiftRight(vm, x, s);
}
E7_NATIVE(bi_cmp) { E7_UNUSED; r->f = bigCmp(Big(vm, a[0]), Big(vm, a[1])); }
E7_NATIVE(bi_toString) {
  E7_UNUSED;
  double rd = toInteger(a[1].f);
  if (rd < 2 || rd > 36) vm.error("RangeError: toString() radix must be between 2 and 36");
  r->r = vm.newStringAscii(bigToString(Big(vm, a[0]), (int)rd).c_str());
}
E7_NATIVE(bi_toNumber) { E7_UNUSED; r->f = bigToDouble(Big(vm, a[0])); }
E7_NATIVE(bi_toU64) { E7_UNUSED; r->u = bigToU64(Big(vm, a[0])); }
E7_NATIVE(bi_fromI64) {
  E7_UNUSED;
  int64_t v = a[0].i;
  r->r = v < 0 ? bigFromU64(vm, 0 - (uint64_t)v, true) : bigFromU64(vm, (uint64_t)v, false);
}
E7_NATIVE(bi_fromU64) { E7_UNUSED; r->r = bigFromU64(vm, a[0].u, false); }
E7_NATIVE(bi_fromNumber) {
  E7_UNUSED;
  double d = a[0].f;
  if (!std::isfinite(d) || std::trunc(d) != d)
    vm.error("RangeError: The number %s cannot be converted to a BigInt because it is not an integer", numberToString(d).c_str());
  bool neg = d < 0;
  d = std::fabs(d);
  Mag m;
  int ex;
  double frac = std::frexp(d, &ex);  // d = frac * 2^ex
  if (d < 18446744073709551616.0) {
    r->r = bigFromU64(vm, (uint64_t)d, neg);
    return;
  }
  uint64_t mant = (uint64_t)std::ldexp(frac, 53);
  BigObj* base = bigFromU64(vm, mant, false);
  BigObj* sh = shiftLeft(vm, base, (uint64_t)(ex - 53));
  sh->neg = neg && !sh->mag.empty();
  r->r = sh;
}
E7_NATIVE(bi_fromString) {
  E7_UNUSED;
  std::string s = strUtf8(S(vm, a[0]));
  BigObj* b = bigParse(vm, s, false);
  if (!b) vm.error("SyntaxError: Cannot convert %s to a BigInt", s.c_str());
  r->r = b;
}
E7_NATIVE(bi_parseLiteral) {
  E7_UNUSED;
  std::string s = strUtf8(S(vm, a[0]));
  BigObj* b = bigParse(vm, s, true);
  if (!b) vm.error("invalid bigint literal");
  r->r = b;
}
static void asN(VM& vm, Slot* a, Slot* r, bool isSigned) {
  double bd = toInteger(a[0].f);
  if (bd < 0 || bd > 9007199254740991.0) vm.error("RangeError: Invalid value: not (convertible to) a safe integer");
  uint64_t bits = (uint64_t)bd;
  BigObj* x = Big(vm, a[1]);
  if (bits == 0) { r->r = mk(vm, {}, false); return; }
  if (bits > (1u << 25)) {
    if (!x->neg && x->mag.size() * 32 < bits) { r->r = x; return; }
    vm.error("RangeError: Maximum BigInt size exceeded");
  }
  size_t n = (size_t)((bits + 31) / 32) + 1;
  Mag t = toTwos(x, std::max(n, x->mag.size() + 1));
  t.resize((size_t)((bits + 31) / 32));
  int rem = (int)(bits % 32);
  if (rem) t.back() &= (1u << rem) - 1;
  trim(t);
  BigObj* u = mk(vm, t, false);
  if (isSigned) {
    // if bit (bits-1) set: result = u - 2^bits
    size_t w = (size_t)((bits - 1) / 32);
    bool top = w < u->mag.size() && (u->mag[w] >> ((bits - 1) % 32)) & 1;
    if (top) {
      BigObj* p = shiftLeft(vm, mk(vm, {1}, false), bits);
      r->r = addSigned(vm, u, p, true);
      return;
    }
  }
  r->r = u;
}
E7_NATIVE(bi_asUintN) { E7_UNUSED; asN(vm, a, r, false); }
E7_NATIVE(bi_asIntN) { E7_UNUSED; asN(vm, a, r, true); }

void registerBigIntNatives(VM& vm) {
  vm.registerNative("BigInt.add", "RR>R", bi_add);
  vm.registerNative("BigInt.sub", "RR>R", bi_sub);
  vm.registerNative("BigInt.mul", "RR>R", bi_mul);
  vm.registerNative("BigInt.div", "RR>R", bi_div);
  vm.registerNative("BigInt.mod", "RR>R", bi_mod);
  vm.registerNative("BigInt.pow", "RR>R", bi_pow);
  vm.registerNative("BigInt.and", "RR>R", bi_and);
  vm.registerNative("BigInt.or", "RR>R", bi_or);
  vm.registerNative("BigInt.xor", "RR>R", bi_xor);
  vm.registerNative("BigInt.shl", "RR>R", bi_shl);
  vm.registerNative("BigInt.shr", "RR>R", bi_shr);
  vm.registerNative("BigInt.neg", "R>R", bi_neg);
  vm.registerNative("BigInt.not", "R>R", bi_not);
  vm.registerNative("BigInt.cmp", "RR>F", bi_cmp);
  vm.registerNative("BigInt.toString", "RF>R", bi_toString);
  vm.registerNative("BigInt.toNumber", "R>F", bi_toNumber);
  vm.registerNative("BigInt.toU64", "R>I", bi_toU64);
  vm.registerNative("BigInt.fromI64", "I>R", bi_fromI64);
  vm.registerNative("BigInt.fromU64", "I>R", bi_fromU64);
  vm.registerNative("BigInt.fromNumber", "F>R", bi_fromNumber);
  vm.registerNative("BigInt.fromString", "R>R", bi_fromString);
  vm.registerNative("BigInt.parseLiteral", "R>R", bi_parseLiteral);
  vm.registerNative("BigInt.asUintN", "FR>R", bi_asUintN);
  vm.registerNative("BigInt.asIntN", "FR>R", bi_asIntN);
}

}  // namespace e7
