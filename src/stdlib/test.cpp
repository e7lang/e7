// e7:test natives: assertion helpers for `e7 test` (and any script that imports e7:test).
#include <cmath>
#include <cstring>

#include "util.h"

namespace e7 {

// Structural equality used by assertEqual / assertNotEqual: primitives by value (NaN equals NaN),
// strings / bigints / Uint8Arrays by content, arrays element-wise, class instances and object
// literals field by field (same class), Maps / Sets by entries (order-insensitive).
static bool deepEq(VM& vm, Slot a, Slot b, uint8_t vt, int depth) {
  vm.chargeWork(1);
  if (vt == VT_NUMBER) return a.f == b.f || (a.f != a.f && b.f != b.f);
  if (vt != VT_REF) return a.i == b.i;
  Obj* x = a.r;
  Obj* y = b.r;
  if (x == y) return true;
  if (!x || !y || x->type != y->type) return false;
  if (depth > 64) vm.error("assertEqual: values are nested too deeply to compare");
  switch (x->type) {
    case OT::String: return strEq(static_cast<StrObj*>(x), static_cast<StrObj*>(y));
    case OT::BigInt: return bigCmp(static_cast<BigObj*>(x), static_cast<BigObj*>(y)) == 0;
    case OT::Box: {
      auto* bx = static_cast<BoxObj*>(x);
      auto* by = static_cast<BoxObj*>(y);
      if ((bx->flags & 1) != (by->flags & 1)) return false;
      if (kindOfVT(bx->vt) != kindOfVT(by->vt)) return false;
      return deepEq(vm, bx->v, by->v, bx->vt, depth + 1);
    }
    case OT::Bytes: {
      auto* bx = static_cast<BytesObj*>(x);
      auto* by = static_cast<BytesObj*>(y);
      vm.chargeWork(bx->len);
      return taKind(bx) == taKind(by) && bx->len == by->len && (bx->len == 0 || memcmp(bx->ptr(), by->ptr(), bx->len) == 0);
    }
    case OT::Array: {
      auto* ax = static_cast<ArrObj*>(x);
      auto* ay = static_cast<ArrObj*>(y);
      if (ax->len != ay->len || ax->vt != ay->vt) return false;
      for (uint32_t i = 0; i < ax->len; i++)
        if (!deepEq(vm, ax->data[i], ay->data[i], ax->vt, depth + 1)) return false;
      return true;
    }
    case OT::Instance: {
      auto* ix = static_cast<InstObj*>(x);
      auto* iy = static_cast<InstObj*>(y);
      if (ix->cls != iy->cls) return false;
      for (uint32_t i = 0; i < ix->cls->nfields; i++)
        if (!deepEq(vm, ix->fields()[i], iy->fields()[i], ix->cls->fieldVT[i], depth + 1)) return false;
      return true;
    }
    case OT::Map: {
      auto* mx = static_cast<MapObj*>(x);
      auto* my = static_cast<MapObj*>(y);
      if (mx->isSet != my->isSet || mx->count != my->count || mx->kvt != my->kvt || mx->vvt != my->vvt) return false;
      for (size_t i = 0; i < mx->entries.size(); i++) {
        const MapEntry& e = mx->entries[i];
        if (!e.live) continue;
        int32_t j = mapFind(vm, my, e.k);
        if (j < 0) return false;
        if (!mx->isSet && !deepEq(vm, e.v, my->entries[(size_t)j].v, mx->vvt, depth + 1)) return false;
      }
      return true;
    }
    case OT::Handle: return static_cast<HandleObj*>(x)->id == static_cast<HandleObj*>(y)->id;
    default: return false;
  }
}

static std::string show(VM& vm, Slot v, uint8_t vt) {
  std::string s = inspectValue(vm, v, vt, 0, false);
  if (s.size() > 300) s = s.substr(0, 297) + "...";
  return s;
}

// optional message argument (null when omitted)
static std::string msgOf(Slot m) {
  Obj* o = m.r;
  if (o && o->type == OT::String) return strUtf8(static_cast<StrObj*>(o));
  return "";
}

static std::string head(const char* what, Slot m) {
  std::string msg = msgOf(m);
  return std::string(what) + " failed" + (msg.empty() ? "" : ": " + msg);
}

E7_NATIVE(t_assert) {
  E7_UNUSED;
  if (a[0].i) return;
  std::string msg = msgOf(a[1]);
  vm.error("%s", msg.empty() ? "assertion failed" : ("assertion failed: " + msg).c_str());
}

E7_NATIVE(t_fail) {
  E7_UNUSED;
  std::string msg = msgOf(a[0]);
  vm.error("%s", msg.empty() ? "test failed" : ("test failed: " + msg).c_str());
}

// args: actual, expected, flags (vt + 32 * negate), message
// (the value type comes from bytecode: it must agree with the kind of the native's arguments)
static void equalImpl(VM& vm, Slot* a, Kind kind) {
  int flags = immIndex(a[2].f, 64);
  if (flags < 0) vm.error("invalid assertion flags");
  uint8_t vt = (uint8_t)(flags & 31);
  if (vt >= VT__COUNT || kindOfVT(vt) != kind) vm.error("invalid assertion flags");
  bool negate = flags >= 32;
  bool eq = deepEq(vm, a[0], a[1], vt, 0);
  if (eq != negate) return;
  std::string m;
  if (negate) m = head("assertNotEqual", a[3]) + "\n  both values: " + show(vm, a[0], vt);
  else m = head("assertEqual", a[3]) + "\n  expected: " + show(vm, a[1], vt) + "\n  actual:   " + show(vm, a[0], vt);
  vm.error("%s", m.c_str());
}
E7_NATIVE(t_equalF) { E7_UNUSED; equalImpl(vm, a, K_F); }
E7_NATIVE(t_equalI) { E7_UNUSED; equalImpl(vm, a, K_I); }
E7_NATIVE(t_equalR) { E7_UNUSED; equalImpl(vm, a, K_R); }

E7_NATIVE(t_close) {
  E7_UNUSED;
  double x = a[0].f, y = a[1].f, tol = a[2].f;
  if (std::fabs(x - y) <= tol || x == y) return;
  std::string m = head("assertClose", a[3]) + "\n  expected: " + numberToString(y) + " (tolerance " + numberToString(tol) +
                  ")\n  actual:   " + numberToString(x);
  vm.error("%s", m.c_str());
}

// assertThrows(fn, expectedMessage?): runtime errors (throw, abort, failed assertions, index
// errors, ...) raised by fn are caught; budget, memory and stack exhaustion are not.
E7_NATIVE(t_throws) {
  E7_UNUSED;
  CloObj* c = Clo(vm, a[0]);
  if (c->fn->nparams != 0) vm.error("assertThrows expects a function without parameters");
  std::string want = msgOf(a[1]);
  std::string got;
  VM::TempRoot keep(vm, c);
  if (vm.callClosureCatching(c, got)) vm.error("%s", (head("assertThrows", Slot{}) + ": the function did not throw").c_str());
  size_t cut = got.find("\n    at ");
  if (cut != std::string::npos) got.resize(cut);
  if (!want.empty() && got.find(want) == std::string::npos) {
    std::string m = head("assertThrows", Slot{}) + "\n  expected an error containing: " + want + "\n  actual error: " + got;
    vm.error("%s", m.c_str());
  }
}

void registerTestNatives(VM& vm) {
  vm.registerNative("test.assert", "IR>V", t_assert);
  vm.registerNative("test.fail", "R>V", t_fail);
  vm.registerNative("test.equalF", "FFFR>V", t_equalF);
  vm.registerNative("test.equalI", "IIFR>V", t_equalI);
  vm.registerNative("test.equalR", "RRFR>V", t_equalR);
  vm.registerNative("test.close", "FFFR>V", t_close);
  vm.registerNative("test.throws", "RR>V", t_throws);
}

}  // namespace e7
