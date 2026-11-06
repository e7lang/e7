// The e7vm interpreter loop.
#include <cmath>
#include <cstring>

#include "../../third_party/fdlibm/ieee754.h"
#include "../core/numfmt.h"
#include "debug.h"
#include "vm.h"

// Computed-goto dispatch. LLVM 15-17 (clang 15-17, Apple clang 15/16 = Xcode 15/16, NDK r26 and
// older) miscompile this loop with computed goto: CodeGenPrepare's address-mode sinking gives loads
// of trailing instruction words the wrong address. Our CMake build disables that transformation
// for this file (-mllvm -addr-sink-using-gep=false) and defines E7_LLVM_CGP_WORKAROUND; any other
// build of those compilers falls back to the switch loop, which is correct everywhere.
#if defined(E7_NO_COMPUTED_GOTO)
#define E7_COMPUTED_GOTO 0
#elif defined(__clang__)
#if defined(E7_LLVM_CGP_WORKAROUND)
#define E7_COMPUTED_GOTO 1
#elif defined(__apple_build_version__)
#define E7_COMPUTED_GOTO (__clang_major__ >= 17)
#else
#define E7_COMPUTED_GOTO (__clang_major__ >= 18)
#endif
#elif defined(__GNUC__)
#define E7_COMPUTED_GOTO 1
#else
#define E7_COMPUTED_GOTO 0
#endif

namespace e7 {

const OpInfo kOpInfo[OP__COUNT] = {
#define E7_X(name, fmt, kind) {#name, fmt, kind},
    E7_OPCODES(E7_X)
#undef E7_X
};

static inline int32_t toInt32(double d) { return (int32_t)(uint32_t)(uint64_t)doubleToInt64Bits(d); }
static inline uint32_t toUint32(double d) { return (uint32_t)(uint64_t)doubleToInt64Bits(d); }

// x ** y: the fdlibm pow V8 uses (with the ECMAScript special cases), not the C library's.
static inline double jsPow(double x, double y) { return fdlibm::pow(x, y); }

static inline uint32_t rotl32(uint32_t x, unsigned n) { n &= 31; return n ? (x << n) | (x >> (32 - n)) : x; }
static inline uint64_t rotl64(uint64_t x, unsigned n) { n &= 63; return n ? (x << n) | (x >> (64 - n)) : x; }

static inline int clz64(uint64_t x) {
  if (!x) return 64;
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_clzll(x);
#else
  int n = 0;
  while (!(x & 0x8000000000000000ull)) { x <<= 1; n++; }
  return n;
#endif
}
static inline int ctz64(uint64_t x) {
  if (!x) return 64;
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctzll(x);
#else
  int n = 0;
  while (!(x & 1)) { x >>= 1; n++; }
  return n;
#endif
}
static inline int popcnt64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcountll(x);
#else
  int n = 0;
  while (x) { x &= x - 1; n++; }
  return n;
#endif
}
static inline uint64_t mulhiu(uint64_t a, uint64_t b) {
#if defined(__SIZEOF_INT128__)
  return (uint64_t)(((unsigned __int128)a * b) >> 64);
#else
  uint64_t a0 = (uint32_t)a, a1 = a >> 32, b0 = (uint32_t)b, b1 = b >> 32;
  uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  uint64_t mid = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
  return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
#endif
}
static inline int64_t mulhis(int64_t a, int64_t b) {
  uint64_t h = mulhiu((uint64_t)a, (uint64_t)b);
  if (a < 0) h -= (uint64_t)b;
  if (b < 0) h -= (uint64_t)a;
  return (int64_t)h;
}

static inline uint16_t argReg(const uint64_t* words, uint32_t i) {
  return (uint16_t)(words[i >> 2] >> (16 * (i & 3)));
}

Slot VM::invokeNativeMany(NativeBinding& nb, Slot* R, const uint64_t* aw, uint32_t argc) {
  std::vector<Slot> args(argc);
  for (uint32_t k = 0; k < argc; k++) args[k] = R[argReg(aw, k)];
  Slot ret;
  ret.u = 0;
  nativeDepth_++;
  gcBlockers_++;
  const NativeBinding* prevNative = curNative_;
  curNative_ = &nb;
  if (nb.cost != NC_NONE) callCharged(nb, args.data(), argc, &ret);
  else nb.fn(*this, args.data(), argc, &ret, nb.ud);
  curNative_ = prevNative;
  gcBlockers_--;
  nativeDepth_--;
  return ret;
}

// Calls a native with a cost model and charges its work, also when it fails: since try/catch, a
// script can call a failing native in a loop, so the work done before the error must count.
void VM::callCharged(NativeBinding& nb, Slot* args, uint32_t argc, Slot* ret) {
  try {
    nb.fn(*this, args, argc, ret, nb.ud);
  } catch (...) {
    Slot none;
    none.u = 0;
    chargeNative(nb, args, argc, none);
    throw;
  }
  chargeNative(nb, args, argc, *ret);
}

StrObj* VM::intToStringObj(Slot v, uint8_t vt) {
  std::string s;
  if (vt == VT_U64) s = uint64ToString(v.u);
  else if (vt == VT_BOOL) s = v.i ? "true" : "false";
  else s = int64ToString(v.i);
  return newStringAscii(s.c_str());
}

StrObj* VM::numToStringObj(double d) { return newStringAscii(numberToString(d).c_str()); }

#define E7_INTERP_NAME runNormal
#define E7_INTERP_DBG 0
#include "interp_loop.inc"
#undef E7_INTERP_NAME
#undef E7_INTERP_DBG
#define E7_INTERP_NAME runDebug
#define E7_INTERP_DBG 1
#include "interp_loop.inc"
#undef E7_INTERP_NAME
#undef E7_INTERP_DBG

// Runs until the entry frame at entryDepth returns. A catchable error (try/catch) raised in a frame
// of this call with a handler continues at the handler; other errors propagate to the caller (the
// host, or the native that called back into script). The C++ try block costs nothing until an
// error is actually thrown.
int VM::execute(size_t entryDepth) {
  const ExecState st{nativeDepth_, gcBlockers_, curNative_, callbackNatives_.size(), tempRoots_.size(), gcInhibit_};
  for (;;) {
    try {
      if (E7_UNLIKELY(pendingThrow_ != nullptr)) {
        // an awaited promise was rejected: the error is raised at the AWAIT of the resumed function
        Obj* t = pendingThrow_;
        pendingThrow_ = nullptr;
        raiseThrown(t);
      }
      return E7_UNLIKELY(dbg_ != nullptr) ? runDebug() : runNormal();
    } catch (E7Error& e) {
      if (e.jsName.empty()) throw;
      int c = catchError(e, entryDepth, st);
      if (c == 0) throw;
      if (c == 2) return ERR_PAUSED;  // a debugger stop before the handler (pause-to-host)
      if (c == 3) return ERR_OK;      // an async entry frame rejected its promise and returned it
    }
  }
}

}  // namespace e7
