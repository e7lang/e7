// Math natives (JavaScript semantics).
#include "util.h"

namespace e7 {

#define MATH1(nm, expr) \
  static void m_##nm(VM& vm, Slot* a, uint32_t n, Slot* r, void* ud) { E7_UNUSED; double x = a[0].f; r->f = (expr); }

static double jsRound(double x) {
  if (!std::isfinite(x) || x == 0) return x;
  if (x > 0 && x < 0.5) return 0.0;
  if (x < 0 && x >= -0.5) return -0.0;
  double f = std::floor(x);
  return (x - f >= 0.5) ? f + 1 : f;
}
static double jsSign(double x) { return x != x ? NAN : x > 0 ? 1 : x < 0 ? -1 : x; }

MATH1(abs, std::fabs(x))
MATH1(ceil, std::ceil(x))
MATH1(floor, std::floor(x))
MATH1(round, jsRound(x))
MATH1(trunc, std::trunc(x))
MATH1(sign, jsSign(x))
MATH1(sqrt, std::sqrt(x))
MATH1(cbrt, std::cbrt(x))
MATH1(exp, std::exp(x))
MATH1(expm1, std::expm1(x))
MATH1(log, std::log(x))
MATH1(log1p, std::log1p(x))
MATH1(log2, std::log2(x))
MATH1(log10, std::log10(x))
MATH1(sin, std::sin(x))
MATH1(cos, std::cos(x))
MATH1(tan, std::tan(x))
MATH1(asin, std::asin(x))
MATH1(acos, std::acos(x))
MATH1(atan, std::atan(x))
MATH1(sinh, std::sinh(x))
MATH1(cosh, std::cosh(x))
MATH1(tanh, std::tanh(x))
MATH1(asinh, std::asinh(x))
MATH1(acosh, std::acosh(x))
MATH1(atanh, std::atanh(x))
MATH1(fround, (double)(float)x)
MATH1(clz32, (double)([](uint32_t v) { int c = 0; if (!v) return 32; while (!(v & 0x80000000u)) { v <<= 1; c++; } return c; })((uint32_t)doubleToInt64Bits(x)))

E7_NATIVE(m_atan2) { E7_UNUSED; r->f = std::atan2(a[0].f, a[1].f); }
E7_NATIVE(m_pow) {
  E7_UNUSED;
  double x = a[0].f, y = a[1].f;
  if (y != y) r->f = NAN;
  else if (y == 0) r->f = 1;
  else if ((x == 1 || x == -1) && std::isinf(y)) r->f = NAN;
  else r->f = std::pow(x, y);
}
E7_NATIVE(m_imul) {
  E7_UNUSED;
  int32_t x = (int32_t)(uint32_t)doubleToInt64Bits(a[0].f), y = (int32_t)(uint32_t)doubleToInt64Bits(a[1].f);
  r->f = (double)(int32_t)((uint32_t)x * (uint32_t)y);
}
E7_NATIVE(m_random) { E7_UNUSED; r->f = vm.randomDouble(); }
static double jsMin(double x, double y) {
  if (x != x || y != y) return NAN;
  if (x == 0 && y == 0) return std::signbit(x) ? x : y;
  return x < y ? x : y;
}
static double jsMax(double x, double y) {
  if (x != x || y != y) return NAN;
  if (x == 0 && y == 0) return std::signbit(x) ? y : x;
  return x > y ? x : y;
}
E7_NATIVE(m_min) { E7_UNUSED; r->f = jsMin(a[0].f, a[1].f); }
E7_NATIVE(m_max) { E7_UNUSED; r->f = jsMax(a[0].f, a[1].f); }
E7_NATIVE(m_minArray) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  double v = INFINITY;
  for (uint32_t i = 0; i < arr->len; i++) v = jsMin(v, arr->data[i].f);
  r->f = v;
}
E7_NATIVE(m_maxArray) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  double v = -INFINITY;
  for (uint32_t i = 0; i < arr->len; i++) v = jsMax(v, arr->data[i].f);
  r->f = v;
}
E7_NATIVE(m_hypotArray) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  bool inf = false, nan = false;
  double mx = 0;
  for (uint32_t i = 0; i < arr->len; i++) {
    double d = arr->data[i].f;
    if (std::isinf(d)) inf = true;
    else if (d != d) nan = true;
    else mx = std::max(mx, std::fabs(d));
  }
  if (inf) { r->f = INFINITY; return; }
  if (nan) { r->f = NAN; return; }
  if (mx == 0) { r->f = 0; return; }
  // scaled sum with compensation (as in V8)
  double sum = 0, comp = 0;
  for (uint32_t i = 0; i < arr->len; i++) {
    double d = arr->data[i].f / mx;
    double s = d * d - comp;
    double prel = sum + s;
    comp = (prel - sum) - s;
    sum = prel;
  }
  r->f = std::sqrt(sum) * mx;
}
E7_NATIVE(m_idiv) { E7_UNUSED; r->f = std::trunc(a[0].f / a[1].f); }
E7_NATIVE(m_imod) { E7_UNUSED; r->f = std::fmod(a[0].f, a[1].f); }

void registerMathNatives(VM& vm) {
  struct E { const char* n; NativeFn f; };
  static const E ones[] = {{"abs", m_abs}, {"ceil", m_ceil}, {"floor", m_floor}, {"round", m_round}, {"trunc", m_trunc},
                           {"sign", m_sign}, {"sqrt", m_sqrt}, {"cbrt", m_cbrt}, {"exp", m_exp}, {"expm1", m_expm1},
                           {"log", m_log}, {"log1p", m_log1p}, {"log2", m_log2}, {"log10", m_log10}, {"sin", m_sin},
                           {"cos", m_cos}, {"tan", m_tan}, {"asin", m_asin}, {"acos", m_acos}, {"atan", m_atan},
                           {"sinh", m_sinh}, {"cosh", m_cosh}, {"tanh", m_tanh}, {"asinh", m_asinh}, {"acosh", m_acosh},
                           {"atanh", m_atanh}, {"fround", m_fround}, {"clz32", m_clz32}};
  for (auto& e : ones) vm.registerNative(std::string("Math.") + e.n, "F>F", e.f);
  vm.registerNative("Math.atan2", "FF>F", m_atan2);
  vm.registerNative("Math.pow", "FF>F", m_pow);
  vm.registerNative("Math.imul", "FF>F", m_imul);
  vm.registerNative("Math.random", ">F", m_random);
  vm.registerNative("Math.min", "FF>F", m_min);
  vm.registerNative("Math.max", "FF>F", m_max);
  vm.registerNative("Math.minArray", "R>F", m_minArray);
  vm.registerNative("Math.maxArray", "R>F", m_maxArray);
  vm.registerNative("Math.hypotArray", "R>F", m_hypotArray);
  vm.registerNative("Math.idiv", "FF>F", m_idiv);
  vm.registerNative("Math.imod", "FF>F", m_imod);
}

}  // namespace e7
