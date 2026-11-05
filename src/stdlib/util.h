// Helpers shared by native library implementations.
#pragma once
#include <cmath>
#include <string>

#include "../core/numfmt.h"
#include "../vm/vm.h"

namespace e7 {

#define E7_NATIVE(name) static void name(VM& vm, Slot* a, uint32_t n, Slot* r, void* ud)
#define E7_UNUSED (void)vm; (void)a; (void)n; (void)r; (void)ud

inline StrObj* S(VM& vm, Slot s) { return asStr(vm, s); }
inline ArrObj* A(VM& vm, Slot s) {
  if (E7_UNLIKELY(!s.r || s.r->type != OT::Array)) vm.error(s.r ? "expected an array" : "cannot read properties of null or undefined (array)");
  return static_cast<ArrObj*>(s.r);
}
// A small non-negative integer immediate supplied by bytecode: returns -1 unless 0 <= d < n
// (checked before converting, since converting NaN/out-of-range doubles is undefined behaviour).
inline int immIndex(double d, int n) { return d >= 0 && d < n ? (int)d : -1; }
inline BytesObj* B(VM& vm, Slot s) {
  if (E7_UNLIKELY(!s.r || s.r->type != OT::Bytes)) vm.error(s.r ? "expected a Uint8Array" : "cannot read properties of null or undefined (Uint8Array)");
  return static_cast<BytesObj*>(s.r);
}
inline BufObj* Buf(VM& vm, Slot s) {
  if (E7_UNLIKELY(!s.r || s.r->type != OT::Buffer)) vm.error("expected an ArrayBuffer");
  return static_cast<BufObj*>(s.r);
}
inline MapObj* Mo(VM& vm, Slot s) {
  if (E7_UNLIKELY(!s.r || s.r->type != OT::Map)) vm.error(s.r ? "expected a Map or Set" : "cannot read properties of null or undefined (Map)");
  return static_cast<MapObj*>(s.r);
}
inline BigObj* Big(VM& vm, Slot s) {
  if (E7_UNLIKELY(!s.r || s.r->type != OT::BigInt)) vm.error("expected a bigint");
  return static_cast<BigObj*>(s.r);
}
inline CloObj* Clo(VM& vm, Slot s) {
  if (E7_UNLIKELY(!s.r || s.r->type != OT::Closure)) vm.error("expected a function");
  return static_cast<CloObj*>(s.r);
}

// ToIntegerOrInfinity
inline double toInteger(double d) {
  if (d != d) return 0;
  if (std::isinf(d)) return d;
  return std::trunc(d);
}
// Relative index (slice-style): negative counts from end, clamped to [0, len]
inline int64_t relIndex(double v, int64_t len) {
  double d = toInteger(v);
  if (d < 0) {
    d += (double)len;
    return d < 0 ? 0 : (int64_t)d;
  }
  return d > (double)len ? len : (int64_t)d;
}
// Clamped index (substring-style): [0, len]
inline int64_t clampIndex(double v, int64_t len) {
  double d = toInteger(v);
  if (d < 0) return 0;
  return d > (double)len ? len : (int64_t)d;
}

// A #private class field (named "#x" by the compiler): not an own enumerable property in
// JavaScript, so Object.keys, JSON.stringify and console.log skip it. Records (object literals)
// never have private fields; their keys may start with '#' ({ "#a": 1 }).
inline bool privateField(const ClassRT* c, uint32_t i) {
  return !c->isRecord && i < c->fieldNames.size() && !c->fieldNames[i].empty() && c->fieldNames[i][0] == '#';
}

inline Slot slotF(double d) { Slot s; s.f = d; return s; }
inline Slot slotI(int64_t i) { Slot s; s.i = i; return s; }
inline Slot slotR(Obj* o) { Slot s; s.r = o; return s; }

// value formatting / comparison helpers (core.cpp)
std::u16string valueToString(VM& vm, Slot v, uint8_t vt);   // JS String(x)
std::string inspectValue(VM& vm, Slot v, uint8_t vt, int depth, bool top);
bool strictEquals(VM& vm, Slot a, Slot b, uint8_t vt);
bool sameValueZero(VM& vm, Slot a, Slot b, uint8_t vt);
std::u16string intToU16(int64_t v, uint8_t vt);
Slot boxValue(VM& vm, Slot v, uint8_t vt);  // box primitives into R (refs unchanged)
// BigInt helpers (bigint.cpp)
std::string bigToString(const BigObj* b, int radix);
BigObj* bigFromU64(VM& vm, uint64_t v, bool neg);
int bigCmp(const BigObj* a, const BigObj* b);
uint64_t bigToU64(const BigObj* b);  // modulo 2^64 (two's complement for negatives)
BigObj* bigParse(VM& vm, const std::string& s, bool literal);  // nullptr on syntax error
double bigToDouble(const BigObj* b);
// Map helpers (map.cpp)
int32_t mapFind(VM& vm, MapObj* m, Slot k);
// current module of the executing script function
ModuleRT* currentModule(VM& vm);

void jsonMapSet(VM& vm, MapObj* m, Slot k, Slot v);  // map.cpp
}  // namespace e7
