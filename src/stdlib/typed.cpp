// Typed arrays other than Uint8Array: Int8Array ... Float64Array, BigInt64Array, BigUint64Array.
// They are byte views (BytesObj) over an ArrayBuffer with the element kind in Obj::flags; elements are
// little-endian, as on every platform e7vm runs on (JavaScript uses the platform's byte order).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "util.h"

namespace e7 {

// natives here use `n` for lengths: the argument count is nargs_
#define TA_NATIVE(name) static void name(VM& vm, Slot* a, uint32_t nargs_, Slot* r, void* ud)
#define TA_UNUSED (void)vm; (void)a; (void)nargs_; (void)r; (void)ud

const char* taName(uint8_t k) {
  static const char* const n[TA__COUNT] = {"Uint8Array", "Int8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array",
                                          "Int32Array", "Uint32Array", "Float32Array", "Float64Array", "BigInt64Array",
                                          "BigUint64Array"};
  return k < TA__COUNT ? n[k] : "Uint8Array";
}

static uint64_t loadLE(const uint8_t* p, uint32_t n) {
  uint64_t v = 0;
  for (uint32_t k = 0; k < n; k++) v |= (uint64_t)p[k] << (8 * k);
  return v;
}
static void storeLE(uint8_t* p, uint32_t n, uint64_t v) {
  for (uint32_t k = 0; k < n; k++) p[k] = (uint8_t)(v >> (8 * k));
}

double taGetNum(const BytesObj* b, uint32_t i) {
  uint8_t k = taKind(b);
  const uint8_t* p = const_cast<BytesObj*>(b)->ptr() + (size_t)i * taSize(k);
  uint64_t v = loadLE(p, taSize(k));
  switch (k) {
    case TA_I8: return (int8_t)v;
    case TA_I16: return (int16_t)v;
    case TA_U16: return (uint16_t)v;
    case TA_I32: return (int32_t)v;
    case TA_U32: return (uint32_t)v;
    case TA_F32: { uint32_t u = (uint32_t)v; float f; memcpy(&f, &u, 4); return f; }
    case TA_F64: { double d; memcpy(&d, &v, 8); return d; }
    default: return (double)(uint8_t)v;
  }
}
uint64_t taGetRaw64(const BytesObj* b, uint32_t i) { return loadLE(const_cast<BytesObj*>(b)->ptr() + (size_t)i * 8, 8); }

std::string taElemText(const BytesObj* b, uint32_t i) {
  uint8_t k = taKind(b);
  if (k == TA_BI64) return int64ToString((int64_t)taGetRaw64(b, i)) + "n";
  if (k == TA_BU64) return uint64ToString(taGetRaw64(b, i)) + "n";
  double d = taGetNum(b, i);
  return d == 0 && std::signbit(d) ? "-0" : numberToString(d);
}

// ToInt32-style wrapping of a number (ToIntN / ToUintN), and the clamping of Uint8ClampedArray
static uint64_t wrapNum(double d) {
  if (!std::isfinite(d)) return 0;
  double t = std::trunc(d);
  // within int64 the conversion is exact (two's complement gives the value modulo 2^64)
  if (t > -9223372036854775808.0 && t < 9223372036854775808.0) return (uint64_t)(int64_t)t;
  double m = std::fmod(t, 18446744073709551616.0);
  if (m < 0) m += 18446744073709551616.0;
  return m >= 9223372036854775808.0 ? (uint64_t)(m - 9223372036854775808.0) + 9223372036854775808ull : (uint64_t)m;
}
static void taSetNum(BytesObj* b, uint32_t i, double d) {
  uint8_t k = taKind(b);
  uint8_t* p = b->ptr() + (size_t)i * taSize(k);
  switch (k) {
    case TA_U8C: {
      double c = d != d ? 0 : d < 0 ? 0 : d > 255 ? 255 : std::nearbyint(d);  // round half to even
      p[0] = (uint8_t)c;
      return;
    }
    case TA_F32: { float f = (float)d; uint32_t u; memcpy(&u, &f, 4); storeLE(p, 4, u); return; }
    case TA_F64: { uint64_t u; memcpy(&u, &d, 8); storeLE(p, 8, u); return; }
    default: storeLE(p, taSize(k), wrapNum(d)); return;
  }
}

static BytesObj* TA(VM& vm, Slot s) {
  if (E7_UNLIKELY(!s.r || s.r->type != OT::Bytes)) vm.error(s.r ? "expected a typed array" : "cannot read properties of null or undefined (typed array)");
  return static_cast<BytesObj*>(s.r);
}
// the 64-bit element natives: the array's own kind decides the element size (bytecode chooses the native)
static BytesObj* BigTA(VM& vm, Slot s) {
  BytesObj* b = TA(vm, s);
  if (E7_UNLIKELY(!taIsBig(taKind(b)))) vm.error("TypeError: not a BigInt64Array or BigUint64Array");
  return b;
}
static uint8_t kindArg(VM& vm, Slot s) {
  if (s.i < 0 || s.i >= TA__COUNT) vm.error("bad typed array kind");
  return (uint8_t)s.i;
}
static BytesObj* newTA(VM& vm, uint8_t k, double len) {
  if (!(len >= 0) || len != std::trunc(len) || len * taSize(k) > 2147483647.0) vm.error("RangeError: Invalid typed array length: %s", numberToString(len).c_str());
  BytesObj* b = vm.newBytes((uint32_t)len * taSize(k));
  b->flags = (uint16_t)((b->flags & ~0xF) | k);
  return b;
}
static uint32_t index(VM& vm, BytesObj* b, double d) {
  uint32_t n = taLength(b);
  if (!(d >= 0 && d < n) || d != std::trunc(d)) vm.error("index %s out of range (length %u)", numberToString(d).c_str(), (unsigned)n);
  return (uint32_t)d;
}
static int64_t rel(double d, uint32_t len) {
  if (d != d) return 0;
  double t = std::trunc(d);
  if (t < 0) return t + len < 0 ? 0 : (int64_t)(t + len);
  return t > len ? (int64_t)len : (int64_t)t;
}

TA_NATIVE(ta_new) { TA_UNUSED; r->r = newTA(vm, kindArg(vm, a[0]), a[1].f); }
// from a number[] (a bigint[] for the 64-bit kinds) or another typed array
TA_NATIVE(ta_from) {
  TA_UNUSED;
  uint8_t k = kindArg(vm, a[0]);
  Obj* src = a[1].r;
  if (src && src->type == OT::Bytes) {
    auto* s = static_cast<BytesObj*>(src);
    uint32_t n = taLength(s);
    if (taIsBig(k) != taIsBig(taKind(s))) vm.error("TypeError: Cannot mix BigInt and other types, use explicit conversions");
    BytesObj* b = newTA(vm, k, n);
    for (uint32_t i = 0; i < n; i++) {
      if (taIsBig(k)) storeLE(b->ptr() + (size_t)i * 8, 8, taGetRaw64(s, i));
      else taSetNum(b, i, taGetNum(s, i));
    }
    r->r = b;
    return;
  }
  ArrObj* arr = A(vm, a[1]);
  VM::TempRoot keep(vm, arr);
  BytesObj* b = newTA(vm, k, arr->len);
  for (uint32_t i = 0; i < arr->len; i++) {
    if (taIsBig(k)) {
      Obj* e = arr->data[i].r;
      if (!e || e->type != OT::BigInt) vm.error("TypeError: Cannot convert to a BigInt");
      storeLE(b->ptr() + (size_t)i * 8, 8, bigToU64(static_cast<BigObj*>(e)));
    } else {
      taSetNum(b, i, arr->vt == VT_NUMBER ? arr->data[i].f : (double)arr->data[i].i);
    }
  }
  r->r = b;
}
// new X(buffer, byteOffset, length) (length < 0: to the end)
TA_NATIVE(ta_fromBuffer) {
  TA_UNUSED;
  uint8_t k = kindArg(vm, a[0]);
  BufObj* buf = Buf(vm, a[1]);
  uint32_t es = taSize(k);
  double off = a[2].f;
  if (!(off >= 0) || off != std::trunc(off) || off > buf->len) vm.error("RangeError: Start offset %s is outside the bounds of the buffer", numberToString(off).c_str());
  if ((uint64_t)off % es) vm.error("RangeError: start offset of %s should be a multiple of %u", taName(k), (unsigned)es);
  double len = a[3].f;
  uint64_t bytes;
  if (len < 0) {
    if ((buf->len - (uint64_t)off) % es) vm.error("RangeError: byte length of %s should be a multiple of %u", taName(k), (unsigned)es);
    bytes = buf->len - (uint64_t)off;
  } else {
    if (len != std::trunc(len) || off + len * es > buf->len) vm.error("RangeError: Invalid typed array length: %s", numberToString(len).c_str());
    bytes = (uint64_t)len * es;
  }
  BytesObj* b = vm.newBytesView(buf, (uint32_t)off, (uint32_t)bytes);
  b->flags = (uint16_t)((b->flags & ~0xF) | k);
  r->r = b;
}
TA_NATIVE(ta_length) { TA_UNUSED; r->f = taLength(TA(vm, a[0])); }
TA_NATIVE(ta_get) { TA_UNUSED; BytesObj* b = TA(vm, a[0]); if (taIsBig(taKind(b))) vm.error("TypeError: a BigInt typed array"); r->f = taGetNum(b, index(vm, b, a[1].f)); }
TA_NATIVE(ta_set) { TA_UNUSED; BytesObj* b = TA(vm, a[0]); if (taIsBig(taKind(b))) vm.error("TypeError: a BigInt typed array"); taSetNum(b, index(vm, b, a[1].f), a[2].f); }
TA_NATIVE(ta_getBig) {
  TA_UNUSED;
  BytesObj* b = BigTA(vm, a[0]);
  uint64_t v = taGetRaw64(b, index(vm, b, a[1].f));
  bool neg = taKind(b) == TA_BI64 && (int64_t)v < 0;
  r->r = bigFromU64(vm, neg ? 0 - v : v, neg);
}
TA_NATIVE(ta_setBig) {
  TA_UNUSED;
  BytesObj* b = BigTA(vm, a[0]);
  uint32_t i = index(vm, b, a[1].f);
  storeLE(b->ptr() + (size_t)i * 8, 8, bigToU64(Big(vm, a[2])));
}
static BytesObj* subView(VM& vm, BytesObj* b, double s, double e, bool copy) {
  uint32_t n = taLength(b), es = taSize(taKind(b));
  int64_t from = rel(s, n), to = rel(e, n);
  uint32_t cnt = to > from ? (uint32_t)(to - from) : 0;
  BytesObj* out;
  if (copy) {
    out = vm.newBytesFrom(b->ptr() + (size_t)from * es, (size_t)cnt * es);
  } else {
    out = vm.newBytesView(b->buf, b->off + (uint32_t)from * es, cnt * es);
  }
  out->flags = (uint16_t)((out->flags & ~0xF) | taKind(b));
  return out;
}
TA_NATIVE(ta_subarray) { TA_UNUSED; r->r = subView(vm, TA(vm, a[0]), a[1].f, a[2].f, false); }
TA_NATIVE(ta_slice) { TA_UNUSED; r->r = subView(vm, TA(vm, a[0]), a[1].f, a[2].f, true); }
TA_NATIVE(ta_fill) {
  TA_UNUSED;
  BytesObj* b = TA(vm, a[0]);
  uint32_t n = taLength(b);
  int64_t from = rel(a[2].f, n), to = rel(a[3].f, n);
  for (int64_t i = from; i < to; i++) taSetNum(b, (uint32_t)i, a[1].f);
  r->r = b;
}
TA_NATIVE(ta_fillBig) {
  TA_UNUSED;
  BytesObj* b = BigTA(vm, a[0]);
  uint32_t n = taLength(b);
  int64_t from = rel(a[2].f, n), to = rel(a[3].f, n);
  uint64_t v = bigToU64(Big(vm, a[1]));
  for (int64_t i = from; i < to; i++) storeLE(b->ptr() + (size_t)i * 8, 8, v);
  r->r = b;
}
// the elements as a number[] (bigint[] for the 64-bit kinds): iteration, spread, callbacks
TA_NATIVE(ta_toArray) {
  TA_UNUSED;
  BytesObj* b = TA(vm, a[0]);
  VM::TempRoot keep(vm, b);
  uint32_t n = taLength(b);
  uint8_t k = taKind(b);
  ArrObj* arr = vm.newArray(taIsBig(k) ? VT_REF : VT_NUMBER, n);
  VM::TempRoot keepA(vm, arr);
  for (uint32_t i = 0; i < n; i++) {
    if (taIsBig(k)) {
      uint64_t v = taGetRaw64(b, i);
      bool neg = k == TA_BI64 && (int64_t)v < 0;
      BigObj* big = bigFromU64(vm, neg ? 0 - v : v, neg);
      arr->data[i].r = big;
    } else {
      arr->data[i].f = taGetNum(b, i);
    }
    arr->len = i + 1;
  }
  r->r = arr;
}
// target.set(source, offset): source a number[] / bigint[] or a typed array
TA_NATIVE(ta_setFrom) {
  TA_UNUSED;
  BytesObj* b = TA(vm, a[0]);
  double off = a[2].f;
  uint32_t n = taLength(b);
  Obj* src = a[1].r;
  uint32_t cnt = src && src->type == OT::Bytes ? taLength(static_cast<BytesObj*>(src)) : A(vm, a[1])->len;
  if (!(off >= 0) || off != std::trunc(off) || off + cnt > n) vm.error("RangeError: offset is out of bounds");
  uint8_t k = taKind(b);
  if (src->type == OT::Bytes) {
    auto* s = static_cast<BytesObj*>(src);
    if (taIsBig(k) != taIsBig(taKind(s))) vm.error("TypeError: Cannot mix BigInt and other types, use explicit conversions");
    std::vector<uint64_t> tmp(cnt);  // the views may overlap
    std::vector<double> tmpd(cnt);
    for (uint32_t i = 0; i < cnt; i++) {
      if (taIsBig(k)) tmp[i] = taGetRaw64(s, i);
      else tmpd[i] = taGetNum(s, i);
    }
    for (uint32_t i = 0; i < cnt; i++) {
      if (taIsBig(k)) storeLE(b->ptr() + (size_t)(off + i) * 8, 8, tmp[i]);
      else taSetNum(b, (uint32_t)off + i, tmpd[i]);
    }
    return;
  }
  ArrObj* arr = static_cast<ArrObj*>(src);
  for (uint32_t i = 0; i < cnt; i++) {
    if (taIsBig(k)) {
      Obj* e = arr->data[i].r;
      if (!e || e->type != OT::BigInt) vm.error("TypeError: Cannot convert to a BigInt");
      storeLE(b->ptr() + (size_t)(off + i) * 8, 8, bigToU64(static_cast<BigObj*>(e)));
    } else {
      taSetNum(b, (uint32_t)off + i, arr->vt == VT_NUMBER ? arr->data[i].f : (double)arr->data[i].i);
    }
  }
}
TA_NATIVE(ta_indexOf) {
  TA_UNUSED;
  BytesObj* b = TA(vm, a[0]);
  uint32_t n = taLength(b);
  r->f = -1;
  for (int64_t i = rel(a[2].f, n); i < n; i++)
    if (taGetNum(b, (uint32_t)i) == a[1].f) { r->f = (double)i; return; }
}
TA_NATIVE(ta_includes) {
  TA_UNUSED;
  BytesObj* b = TA(vm, a[0]);
  uint32_t n = taLength(b);
  r->i = 0;
  for (uint32_t i = 0; i < n; i++) {
    double v = taGetNum(b, i);
    if (v == a[1].f || (v != v && a[1].f != a[1].f)) { r->i = 1; return; }  // SameValueZero
  }
}
TA_NATIVE(ta_join) {
  TA_UNUSED;
  BytesObj* b = TA(vm, a[0]);
  std::string sep = strUtf8(S(vm, a[1]));
  std::string out;
  uint32_t n = taLength(b);
  for (uint32_t i = 0; i < n; i++) {
    if (i) out += sep;
    std::string t = taElemText(b, i);
    if (taIsBig(taKind(b))) t.pop_back();  // join prints bigints without the "n"
    out += t;
    vm.checkTemp(out.size());
  }
  r->r = vm.newStringUtf8(out);
}
TA_NATIVE(ta_reverse) {
  TA_UNUSED;
  BytesObj* b = TA(vm, a[0]);
  uint32_t n = taLength(b), es = taSize(taKind(b));
  for (uint32_t i = 0; i < n / 2; i++)
    for (uint32_t k = 0; k < es; k++) std::swap(b->ptr()[(size_t)i * es + k], b->ptr()[(size_t)(n - 1 - i) * es + k]);
  r->r = b;
}
// numeric sort (NaN last; -0 before +0), as TypedArray.prototype.sort without a comparator
TA_NATIVE(ta_sort) {
  TA_UNUSED;
  BytesObj* b = TA(vm, a[0]);
  uint32_t n = taLength(b);
  uint8_t k = taKind(b);
  if (taIsBig(k)) {
    std::vector<uint64_t> v(n);
    for (uint32_t i = 0; i < n; i++) v[i] = taGetRaw64(b, i);
    if (k == TA_BI64) std::sort(v.begin(), v.end(), [](uint64_t x, uint64_t y) { return (int64_t)x < (int64_t)y; });
    else std::sort(v.begin(), v.end());
    for (uint32_t i = 0; i < n; i++) storeLE(b->ptr() + (size_t)i * 8, 8, v[i]);
  } else {
    std::vector<double> v(n);
    for (uint32_t i = 0; i < n; i++) v[i] = taGetNum(b, i);
    std::stable_sort(v.begin(), v.end(), [](double x, double y) {
      if (x != x) return false;
      if (y != y) return true;
      if (x == 0 && y == 0) return std::signbit(x) && !std::signbit(y);
      return x < y;
    });
    for (uint32_t i = 0; i < n; i++) taSetNum(b, i, v[i]);
  }
  vm.chargeWork((uint64_t)n * 4);
  r->r = b;
}
TA_NATIVE(ta_buffer) { TA_UNUSED; r->r = TA(vm, a[0])->buf; }

void registerTypedArrayNatives(VM& vm) {
  vm.registerNative("TA.new", "IF>R", ta_new);
  vm.registerNative("TA.from", "IR>R", ta_from);
  vm.registerNative("TA.fromBuffer", "IRFF>R", ta_fromBuffer);
  vm.registerNative("TA.length", "R>F", ta_length);
  vm.registerNative("TA.get", "RF>F", ta_get);
  vm.registerNative("TA.set", "RFF>V", ta_set);
  vm.registerNative("TA.getBig", "RF>R", ta_getBig);
  vm.registerNative("TA.setBig", "RFR>V", ta_setBig);
  vm.registerNative("TA.subarray", "RFF>R", ta_subarray);
  vm.registerNative("TA.slice", "RFF>R", ta_slice);
  vm.registerNative("TA.fill", "RFFF>R", ta_fill);
  vm.registerNative("TA.fillBig", "RRFF>R", ta_fillBig);
  vm.registerNative("TA.toArray", "R>R", ta_toArray);
  vm.registerNative("TA.setFrom", "RRF>V", ta_setFrom);
  vm.registerNative("TA.indexOf", "RFF>F", ta_indexOf);
  vm.registerNative("TA.includes", "RF>I", ta_includes);
  vm.registerNative("TA.join", "RR>R", ta_join);
  vm.registerNative("TA.reverse", "R>R", ta_reverse);
  vm.registerNative("TA.sort", "R>R", ta_sort);
  vm.registerNative("TA.buffer", "R>R", ta_buffer);
}

}  // namespace e7
