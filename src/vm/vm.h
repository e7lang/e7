// e7vm core virtual machine.
#pragma once
#include <atomic>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/module.h"
#include "object.h"

namespace e7 {

class VM;
struct ModuleRT;
class Debugger;  // debug.h

using NativeFn = void (*)(VM& vm, Slot* args, uint32_t nargs, Slot* ret, void* ud);

// How a native's work is charged to the instruction budget (natives cannot be interrupted, so the
// cost is charged after the call; the next budget check then suspends or stops the script).
enum NativeCost : uint8_t {
  NC_NONE,  // O(1) or tiny
  NC_SCAN,  // linear in the sizes of the reference arguments and the result
  NC_OUT,   // linear in the size of the result
  NC_HASH,  // byte-oriented crypto: ~1 tick per byte of input/output
  NC_SORT,  // n log n in the array length
  NC_BIGQ,  // quadratic in BigInt limb counts
};
struct NativeEntry {
  std::string sig;
  NativeFn fn = nullptr;
  void* ud = nullptr;
  uint8_t cost = NC_NONE;
};

struct InlineCache {
  ClassRT* cls = nullptr;
  uint32_t index = 0;  // field index or vtable slot
  uint32_t name = 0;   // string constant index
};

struct FuncRT {
  const FuncDesc* d = nullptr;
  ModuleRT* mod = nullptr;
  std::string name;
  uint32_t nregs = 0;
  uint32_t nparams = 0;
  uint32_t sigId = 0;  // VM-global signature id
  char retKind = 'V';
  const uint64_t* code = nullptr;
  size_t codeLen = 0;
  std::vector<uint16_t> refInit;  // non-param ref registers (null-initialized on entry)
  std::vector<uint16_t> refRegs;  // all ref registers (GC roots)
  std::vector<InlineCache> ics;
  std::vector<uint8_t> dbgFlags;  // per pc, only while a debugger is attached (see debug.h)
};

struct NativeBinding {
  NativeFn fn;
  void* ud;
  char retKind;
  std::string name;
  uint8_t cost = NC_NONE;
  uint64_t refArgs = 0;  // bit k set: argument k is a reference
};

struct ModuleRT {
  Module m;
  std::vector<FuncRT> funcs;
  std::vector<ClassRT> classes;
  std::vector<StrObj*> strs;
  std::vector<Slot> globals;
  std::vector<NativeBinding> natives;
  std::vector<uint32_t> sigMap;
  bool initialized = false;
};

struct Frame {
  FuncRT* fn;
  const uint64_t* ip;
  CloObj* closure;
  uint32_t base;
  uint16_t retDst;
  bool entry;  // returning from this frame leaves execute()
};

struct E7Error : std::exception {
  int code;
  std::string msg;
  E7Error(int c, std::string m) : code(c), msg(std::move(m)) {}
  const char* what() const noexcept override { return msg.c_str(); }
};

// Error codes (mirror e7.h)
enum ErrCode {
  ERR_OK = 0, ERR_SUSPENDED = 1, ERR_RUNTIME = 2, ERR_BUDGET = 3, ERR_MEMORY = 4,
  ERR_STACK = 5, ERR_LOAD = 6, ERR_VERIFY = 7, ERR_SIGNATURE = 8, ERR_NOT_FOUND = 9,
  ERR_ARGS = 10, ERR_COMPILE = 11, ERR_STATE = 12,
  ERR_PAUSED = 13  // stopped by the debugger (pause-to-host mode): frames are kept, resume() continues
};

// A source position (file name as shown in stack traces, 1-based line, column 0 = unknown).
struct SrcLoc {
  std::string file;
  uint32_t line = 0, col = 0;
};

struct VMConfig {
  size_t memoryLimit = 256u << 20;   // bytes
  uint32_t stackSlots = 1u << 18;    // register slots
  uint32_t maxFrames = 20000;
  bool suspendOnBudget = false;
};

class VM {
 public:
  explicit VM(const VMConfig& cfg = VMConfig());
  ~VM();
  VM(const VM&) = delete;
  VM& operator=(const VM&) = delete;

  // ---- natives ----
  void registerNative(const std::string& name, const std::string& sig, NativeFn fn, void* ud = nullptr);
  const NativeEntry* findNative(const std::string& name) const;

  // ---- modules ----
  // Takes ownership of module; verifies, links natives. Throws E7Error.
  ModuleRT* loadModule(Module&& m);
  int runInit(ModuleRT* mod);  // runs module initializer (may suspend)

  // ---- execution ----
  // Call function (entry from host). Returns ERR_OK / ERR_SUSPENDED or throws E7Error.
  int callEntry(FuncRT* f, const Slot* args, uint32_t nargs);
  int resume();
  bool suspended() const { return suspended_; }
  Slot result() const { return result_; }
  char resultKind() const { return resultKind_; }
  void setBudget(int64_t ticks) { budget_ = ticks <= 0 ? INT64_MAX : ticks; }
  // Natives doing work proportional to a script-chosen parameter charge it against the instruction
  // budget up front (a native cannot be suspended halfway), so the budget also bounds them.
  void chargeWork(uint64_t ticks) {
    if (budget_ == INT64_MAX) return;
    if (ticks > (uint64_t)budget_) {
      budget_ = 0;
      errorCode(ERR_BUDGET, "instruction budget exhausted");
    }
    budget_ -= (int64_t)ticks;
  }
  void setSuspendOnBudget(bool b) { cfg_.suspendOnBudget = b; }
  void resetExecution();  // abandon a suspended call

  // Re-entrant call from native code (cannot suspend).
  Slot callClosure(CloObj* c, const Slot* args, uint32_t nargs);
  // Calls a parameterless closure from native code; a runtime error (throw, abort, ...) raised by
  // it is caught: returns false with the error message. Other errors (budget, memory, stack)
  // propagate.
  bool callClosureCatching(CloObj* c, std::string& errorMessage);

  // ---- errors ----
  [[noreturn]] void error(const char* fmt, ...);
  [[noreturn]] void errorCode(int code, const char* fmt, ...);
  std::string stackTrace() const;
  // Source position of instruction `pc` of f (columns need debug information).
  static SrcLoc sourceAt(const FuncRT* f, ptrdiff_t pc);

  // ---- debugging (debug.cpp) ----
  // Attaches a debugger (the VM owns it). Execution then uses the debug interpreter; without a
  // debugger the normal interpreter runs with no debugging cost at all.
  void attachDebugger(std::unique_ptr<Debugger> d);
  void detachDebugger();
  Debugger* debugger() const { return dbg_.get(); }
  Slot* stackBase() const { return stack_; }

  // ---- heap ----
  StrObj* newString(const char16_t* s, size_t len);
  StrObj* newString(const std::u16string& s) { return newString(s.data(), s.size()); }
  StrObj* newStringUtf8(const std::string& s);
  StrObj* newStringAscii(const char* s);
  StrObj* allocString(size_t len);  // uninitialized chars
  ArrObj* newArray(uint8_t vt, uint32_t cap);
  void arrayReserve(ArrObj* a, uint32_t cap);
  void arrayPush(ArrObj* a, Slot v);
  BufObj* newBuffer(uint32_t len);
  BytesObj* newBytes(uint32_t len);
  BytesObj* newBytesView(BufObj* b, uint32_t off, uint32_t len);
  BytesObj* newBytesFrom(const uint8_t* p, size_t len);
  InstObj* newInstance(ClassRT* cls);
  CellObj* newCell(uint8_t vt);
  BoxObj* newBox(uint8_t vt, Slot v);
  CloObj* newClosure(FuncRT* fn, uint32_t nup);
  MapObj* newMap(uint8_t kvt, uint8_t vvt, KeyMode km, bool isSet);
  BigObj* newBig();
  HandleObj* newHandle(int64_t id);
  Obj* undef() const { return undef_; }

  // External buffer accounting
  void accountExternal(int64_t delta);

  // Rooting for host-held objects
  void pin(Obj* o);
  void unpin(Obj* o);

  // Temporary roots for native code that calls back into script.
  struct TempRoot {
    VM& vm;
    size_t n;
    TempRoot(VM& v, Obj* o) : vm(v), n(v.tempRoots_.size()) { v.tempRoots_.push_back(o); }
    ~TempRoot() { vm.tempRoots_.resize(n); }
  };

  void collectGarbage();
  size_t bytesAllocated() const { return bytes_; }
  uint64_t gcCount() const { return gcCount_; }

  // Interned string constants: map a signature string to VM-global id
  uint32_t sigId(const std::string& sig);
  const std::string& sigString(uint32_t id) const { return sigs_[id]; }

  // Output hook for print/console.log
  void (*printFn)(void* ud, const char* s, size_t n) = nullptr;
  void* printUd = nullptr;
  void print(const std::string& s);

  // Random seed for Math.random (xorshift128+)
  void seedRandom(uint64_t seed);
  double randomDouble();

  // Host key lookup hook for crypto handles
  void* hostKeyUd = nullptr;
  int (*hostKeyFn)(void* ud, int64_t id, const uint8_t** key, size_t* len) = nullptr;

  const VMConfig& config() const { return cfg_; }
  ModuleRT* currentModule() const { return frames_.empty() ? nullptr : frames_.back().fn->mod; }
  uint32_t nativeDepth() const { return nativeDepth_; }
  void setNativeCost(const std::string& name, uint8_t cost) {
    auto it = natives_.find(name);
    if (it != natives_.end()) it->second.cost = cost;
  }
  // ticks for a completed native call (see NativeCost)
  uint64_t nativeCost(const NativeBinding& nb, const Slot* args, uint32_t argc, Slot ret) const;
  void chargeNative(const NativeBinding& nb, const Slot* args, uint32_t argc, Slot ret) {
    uint64_t t = nativeCost(nb, args, argc, ret);
    budget_ -= (int64_t)(t > (1ull << 40) ? (1ull << 40) : t);  // may go negative: checked at the next tick
  }
  // Natives building large temporary (non-heap) buffers call this so that transient memory is also
  // bounded by the configured memory limit.
  void checkTemp(size_t bytes) {
    if (bytes > cfg_.memoryLimit || bytes_ + bytes > cfg_.memoryLimit)
      errorCode(ERR_MEMORY, "out of memory (limit %zu bytes)", cfg_.memoryLimit);
  }
  // RAII: no collection while host code holds fresh objects only in C++ locals
  struct GCPause {
    VM& v;
    explicit GCPause(VM& vm) : v(vm) { v.gcInhibit_++; }
    ~GCPause() { v.gcInhibit_--; }
  };

  std::vector<std::unique_ptr<ModuleRT>> modules;

 private:
  friend struct Interp;
  friend class Debugger;
  int execute(size_t entryDepth);
  int runFrames();
  int runNormal();  // the interpreter (interp_loop.inc)
  int runDebug();   // the same loop with debugger checks, used only while a debugger is attached
  Slot invokeNativeMany(NativeBinding& nb, Slot* R, const uint64_t* aw, uint32_t argc);
  StrObj* intToStringObj(Slot v, uint8_t vt);
  StrObj* numToStringObj(double d);
  void* allocRaw(OT t, size_t size);
  void maybeGC();
  void markRoots();
  void mark(Obj* o) {
    if (o && !o->marked) { o->marked = 1; gray_.push_back(o); }
  }
  void traceAll();
  void sweep();
  void freeObj(Obj* o);
  [[noreturn]] void raise(int code, const std::string& msg);

  VMConfig cfg_;
  Obj* objects_ = nullptr;
  Obj* undef_ = nullptr;
  size_t bytes_ = 0;
  size_t nextGC_ = 1u << 20;
  uint64_t gcCount_ = 0;
  bool gcRequested_ = false;
  int gcInhibit_ = 0;  // >0 while a multi-step allocation is in progress
  struct NoGC {
    VM& v;
    explicit NoGC(VM& vm) : v(vm) { v.gcInhibit_++; }
    ~NoGC() { v.gcInhibit_--; }
  };
  std::vector<Obj*> gray_;
  std::vector<Obj*> tempRoots_;
  std::unordered_map<Obj*, uint32_t> pins_;

  Slot* stack_ = nullptr;
  std::vector<Frame> frames_;
  int64_t budget_ = INT64_MAX;
  uint32_t nativeDepth_ = 0;
  // native currently executing (nullptr while script code runs) and, for each callback from a
  // native into script, the frame index of the callback and the calling native (stack traces)
  const NativeBinding* curNative_ = nullptr;
  std::vector<std::pair<size_t, const NativeBinding*>> callbackNatives_;
  bool suspended_ = false;
  Slot result_{};
  char resultKind_ = 'V';

  // debugging: the attached debugger, the mask of per-pc flag bits that make the debug interpreter
  // call into it (atomic: e7_debug_pause sets it from other threads), and the number of active
  // callClosureCatching calls (errors raised inside them are caught, so they do not stop)
  std::unique_ptr<Debugger> dbg_;
  std::atomic<uint8_t> dbgMask_{0};
  size_t dbgRetDepth_ = 0;  // stepping: returning below this frame depth stops at the next instruction
  int catchDepth_ = 0;

  std::unordered_map<std::string, NativeEntry> natives_;
  std::unordered_map<std::string, uint32_t> sigIds_;
  std::vector<std::string> sigs_;
  uint64_t rng_[2] = {0x9E3779B97F4A7C15ull, 0xBF58476D1CE4E5B9ull};
};

// Helpers used by natives
inline StrObj* asStr(VM& vm, Slot s) {
  if (E7_UNLIKELY(!s.r || s.r->type != OT::String)) vm.error("expected string, got %s", s.r ? "object" : "null");
  return static_cast<StrObj*>(s.r);
}
uint32_t strHash(StrObj* s);
bool strEq(const StrObj* a, const StrObj* b);
int strCmp(const StrObj* a, const StrObj* b);
std::u16string strU16(const StrObj* s);
std::string strUtf8(const StrObj* s);

// Registration of builtin native libraries
void registerCoreNatives(VM& vm);
void registerStringNatives(VM& vm);
void registerArrayNatives(VM& vm);
void registerMapNatives(VM& vm);
void registerMathNatives(VM& vm);
void registerBytesNatives(VM& vm);
void registerBigIntNatives(VM& vm);
void registerJsonNatives(VM& vm);
void registerCryptoNatives(VM& vm);
void registerDateNatives(VM& vm);
void registerTestNatives(VM& vm);
void applyNativeCosts(VM& vm);  // core.cpp

}  // namespace e7
