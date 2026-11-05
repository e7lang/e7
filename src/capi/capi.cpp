// C API implementation.
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "../core/since.h"
#include "../crypto/sign.h"
#include "../stdlib/util.h"
#include "../vm/debug.h"
#include "../vm/vm.h"
#include "e7/e7.h"
#ifdef E7_WITH_COMPILER
#include "../compiler/compiler.h"
#endif

using namespace e7;

struct NativeThunk {
  e7_vm* owner;
  e7_native_fn fn;
  void* user;
  std::string hostSig;
};

struct e7_module {
  ModuleRT* rt;
};

// debugger state of the C API: the callback and the storage behind returned pointers
struct DebugApi {
  e7_debug_fn fn = nullptr;
  void* user = nullptr;
  std::vector<DebugFrame> frames;
  std::vector<e7_debug_frame> cframes;
  std::vector<DebugScope> scopes;
  std::vector<e7_debug_scope> cscopes;
  std::vector<DebugVar> vars;
  std::vector<e7_debug_variable> cvars;
  DebugVar evalVar;
  e7_debug_variable cEval{};
  DebugFrame stopFrame;
  std::string stopMsg;
  std::vector<int> stopBps;
};

struct e7_vm {
  std::unique_ptr<VM> vm;
  std::unique_ptr<DebugApi> dbg;
  ModuleRT* pendingInit = nullptr;  // module whose initializer is paused by the debugger
  std::string lastError;
  std::string nativeError;
  std::string retStr;
  std::vector<uint8_t> retBytes;
  std::vector<std::unique_ptr<e7_module>> modules;
  std::vector<std::unique_ptr<NativeThunk>> natives;
  std::vector<uint8_t> trustedKeys;
  bool requireSignature = false;
  char pendingRet = 'v';
  int active = 0;  // >0 while e7_load/e7_call/e7_resume is executing script code
  void (*printFn)(void*, const char*, size_t) = nullptr;
  void* printUd = nullptr;
  // per-native-call argument storage
  std::vector<std::string> argStrings;
};

static char kindOfHost(char c) {
  switch (c) {
    case 'n': return 'F';
    case 'b': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': return 'I';
    case 's': case 'y': case 'h': return 'R';
    case 'v': return 'V';
    default: return 0;
  }
}

static e7_status fail(e7_vm* v, e7_status st, const std::string& msg) {
  v->lastError = msg;
  return st;
}

// Copies a caller's struct that starts with struct_size into a zero-initialized full-size one:
// fields a (smaller) older struct does not have keep their defaults. False if struct_size is
// smaller than the struct_size field itself or larger than this library's struct.
template <class T>
static bool copySized(const T* in, T& out) {
  size_t n = in->struct_size;
  if (n < sizeof(size_t) || n > sizeof(T)) return false;
  memcpy(&out, in, n);
  out.struct_size = sizeof(T);
  return true;
}

extern "C" {

const char* e7_version(void) { return "0.2.0"; }
uint32_t e7_version_number(void) { return kVmVersion; }

void e7_config_init(e7_config* cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->struct_size = sizeof *cfg;
  cfg->memory_limit = 256u << 20;
  cfg->stack_slots = 1u << 18;
  cfg->max_frames = 20000;
  cfg->gc_growth_percent = 200;
}

e7_vm* e7_vm_new(const e7_config* user) {
  e7_config def;
  e7_config_init(&def);
  e7_config given;
  e7_config_init(&given);
  if (user && !copySized(user, given)) return nullptr;
  const e7_config* cfg = &given;
  std::unique_ptr<e7_vm> owner;
  try {
    owner.reset(new e7_vm());
    e7_vm* v = owner.get();
    VMConfig vc;
    vc.memoryLimit = cfg->memory_limit ? cfg->memory_limit : def.memory_limit;
    vc.stackSlots = cfg->stack_slots ? cfg->stack_slots : def.stack_slots;
    vc.maxFrames = cfg->max_frames ? cfg->max_frames : def.max_frames;
    vc.suspendOnBudget = cfg->suspend_on_budget != 0;
    vc.gcGrowthPercent = cfg->gc_growth_percent ? (cfg->gc_growth_percent < 125 ? 125 : cfg->gc_growth_percent) : 200;
    v->vm.reset(new VM(vc));
    v->requireSignature = cfg->require_signature != 0;
    if (cfg->trusted_keys && cfg->num_trusted_keys)
      v->trustedKeys.assign(cfg->trusted_keys, cfg->trusted_keys + 32 * cfg->num_trusted_keys);
    return owner.release();
  } catch (...) {  // owner frees the partially constructed VM
    return nullptr;
  }
}

void e7_vm_free(e7_vm* v) { delete v; }

void e7_free(void* p) { free(p); }

// The VM is not re-entrant: a host native must not call back into e7_load/e7_call/e7_resume or
// mutate execution state (abort, collect) while script code is running.
struct ActiveGuard {
  e7_vm* v;
  explicit ActiveGuard(e7_vm* vm) : v(vm) { v->active++; }
  ~ActiveGuard() { v->active--; }
};
static const char* kReentry = "the VM is running (e7 API called from inside a host function)";

static e7_status loadBytes(e7_vm* v, const uint8_t* data, size_t len, e7_module** out) {
  if (out) *out = nullptr;
  if (v->active) return fail(v, E7_ERR_STATE, kReentry);
  ActiveGuard active_(v);
  if (out) *out = nullptr;
  if (!data) return fail(v, E7_ERR_LOAD, "no data");
  size_t payload = len;
  int sig = checkModuleSignature(data, len, v->trustedKeys.data(), v->trustedKeys.size() / 32, &payload);
  // Signing is opt-in on the receiving side: with no trusted keys configured (and require_signature
  // off) a signed module loads like an unsigned one (its trailer is ignored; the CRC still applies).
  // Once the host configures trusted keys, a signature must verify against one of them.
  bool verifying = v->requireSignature || !v->trustedKeys.empty();
  if (sig < 0 && verifying) return fail(v, E7_ERR_SIGNATURE, "module signature is invalid or the signing key is not trusted");
  if (sig == 0 && v->requireSignature) return fail(v, E7_ERR_SIGNATURE, "module is not signed");
  ModuleHeader hdr;
  if (!readModuleHeader(data, payload, hdr)) return fail(v, E7_ERR_LOAD, "not an e7b file");
  if (hdr.format != kFormatVersion)
    return fail(v, E7_ERR_VERSION, "e7b format " + std::to_string(hdr.format) + " is not supported by e7vm " +
                                       versionString(kVmVersion) + " (it reads format " +
                                       std::to_string(kFormatVersion) + "); recompile the module");
  if (hdr.minVm > kVmVersion)
    return fail(v, E7_ERR_VERSION, "the module requires e7vm " + versionString(hdr.minVm) + "; this is e7vm " +
                                       versionString(kVmVersion) + " (compile with e7c --target " +
                                       versionString(kVmVersion) + ")");
  Module m;
  std::string err;
  if (!deserializeModule(data, payload, m, err)) return fail(v, E7_ERR_LOAD, err);
  try {
    ModuleRT* rt = v->vm->loadModule(std::move(m));
    bool susp = v->vm->config().suspendOnBudget;
    v->vm->setSuspendOnBudget(false);
    int st;
    try {
      st = v->vm->runInit(rt);
    } catch (...) {
      v->vm->setSuspendOnBudget(susp);
      throw;
    }
    v->vm->setSuspendOnBudget(susp);
    auto mod = std::make_unique<e7_module>();
    mod->rt = rt;
    if (out) *out = mod.get();
    v->modules.push_back(std::move(mod));
    if (st == ERR_PAUSED) {  // the debugger stopped in top-level code: finished by e7_debug_resume()
      v->pendingInit = rt;
      v->pendingRet = 'v';
      return E7_PAUSED;
    }
    return E7_OK;
  } catch (E7Error& e) {
    return fail(v, (e7_status)e.code, e.msg);
  } catch (std::bad_alloc&) {
    return fail(v, E7_ERR_MEMORY, "out of memory");
  }
}

e7_status e7_load(e7_vm* v, const uint8_t* data, size_t len, e7_module** out) { return loadBytes(v, data, len, out); }

e7_status e7_compile(const char* source, size_t len, const char* file_name, uint8_t** out, size_t* out_len, char** errors) {
  return e7_compile_ex(source, len, file_name, nullptr, out, out_len, errors);
}

void e7_compile_options_init(e7_compile_options* opts) {
  memset(opts, 0, sizeof *opts);
  opts->struct_size = sizeof *opts;
}

e7_status e7_compile_ex(const char* source, size_t len, const char* file_name, const e7_compile_options* userOpts,
                        uint8_t** out, size_t* out_len, char** errors) {
  if (out) *out = nullptr;
  if (out_len) *out_len = 0;
  if (errors) *errors = nullptr;
  e7_compile_options optsCopy;
  e7_compile_options_init(&optsCopy);
  if (userOpts && !copySized(userOpts, optsCopy)) return E7_ERR_ARGS;
  const e7_compile_options* opts = userOpts ? &optsCopy : nullptr;
#ifdef E7_WITH_COMPILER
  if (!out || !out_len || (!source && len)) return E7_ERR_ARGS;
  try {
    CompileOptions co_;
    co_.debugInfo = opts && (opts->flags & E7_COMPILE_DEBUG);
    co_.target = opts ? opts->target_version : 0;
    std::string name = file_name ? file_name : "main.ts";
    std::string src(source ? source : "", len);
    CompileOutput co;
    if (opts && opts->read_file) {
      std::string entry;  // the first file requested is the (normalized) entry file
      co = compileProgram(name, [&](const std::string& path, std::string& content) {
        if (entry.empty()) entry = path;
        if (path == entry) { content = src; return true; }
        char* data = nullptr;
        size_t n = 0;
        if (opts->read_file(opts->read_user, path.c_str(), &data, &n) != 0) { free(data); return false; }
        content.assign(data ? data : "", data ? n : 0);
        free(data);
        return true;
      }, co_);
    } else {
      co = compileSource(name, src, co_);
    }
    if (!co.ok) {
      if (errors) {
        std::string t = co.errorText();
        *errors = (char*)malloc(t.size() + 1);
        if (*errors) memcpy(*errors, t.c_str(), t.size() + 1);
      }
      return E7_ERR_COMPILE;
    }
    std::vector<uint8_t> bytes = serializeModule(co.module);
    *out = (uint8_t*)malloc(bytes.size() ? bytes.size() : 1);
    if (!*out) return E7_ERR_MEMORY;
    memcpy(*out, bytes.data(), bytes.size());
    *out_len = bytes.size();
    return E7_OK;
  } catch (std::bad_alloc&) {
    return E7_ERR_MEMORY;
  }
#else
  (void)source; (void)len; (void)file_name;
  if (errors) {
    const char* m = "compiler not included in this build (E7_WITH_COMPILER=OFF)";
    *errors = (char*)malloc(strlen(m) + 1);
    if (*errors) strcpy(*errors, m);
  }
  return E7_ERR_COMPILE;
#endif
}

e7_status e7_load_source(e7_vm* v, const char* source, size_t len, const char* file_name, e7_module** out) {
  uint8_t* bc = nullptr;
  size_t bl = 0;
  char* errs = nullptr;
  e7_status st = e7_compile(source, len, file_name, &bc, &bl, &errs);
  if (st != E7_OK) {
    v->lastError = errs ? errs : "compile error";
    free(errs);
    return st;
  }
  bool req = v->requireSignature;
  v->requireSignature = false;  // locally compiled source is trusted by the host
  st = loadBytes(v, bc, bl, out);
  v->requireSignature = req;
  free(bc);
  return st;
}

static const ExportDesc* findExport(e7_module* mod, const char* name) {
  for (auto& e : mod->rt->m.exports)
    if (e.name == name) return &e;
  return nullptr;
}

int e7_has_function(e7_module* mod, const char* function) { return mod && function && findExport(mod, function) != nullptr; }

static bool toSlot(e7_vm* v, char code, const e7_value& a, Slot& out, std::string& err) {
  VM& vm = *v->vm;
  // reject out-of-range type tags before reading them as the enum (host bug, but must not be UB)
  int tag;
  static_assert(sizeof(tag) == sizeof(a.type), "e7_type is int-sized");
  memcpy(&tag, &a.type, sizeof tag);
  if (tag < E7_VOID || tag > E7_NULL) { err = "invalid e7_value type"; return false; }
  switch (code) {
    case 'n':
      if (a.type == E7_NUMBER) out.f = a.v.num;
      else if (a.type == E7_INT) out.f = (double)a.v.i;
      else if (a.type == E7_UINT) out.f = (double)a.v.u;
      else { err = "expected a number"; return false; }
      return true;
    case 'b':
      if (a.type != E7_BOOL) { err = "expected a boolean"; return false; }
      out.i = a.v.boolean ? 1 : 0;
      return true;
    case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': {
      uint8_t vt = (uint8_t)(VT_I8 + (code - '1'));
      int64_t x;
      if (a.type == E7_INT) x = a.v.i;
      else if (a.type == E7_UINT) x = (int64_t)a.v.u;
      else if (a.type == E7_NUMBER) x = doubleToInt64Bits(a.v.num);
      else if (a.type == E7_BOOL) x = a.v.boolean ? 1 : 0;
      else { err = "expected an integer"; return false; }
      switch (vt) {
        case VT_I8: x = (int8_t)x; break;
        case VT_U8: x = (uint8_t)x; break;
        case VT_I16: x = (int16_t)x; break;
        case VT_U16: x = (uint16_t)x; break;
        case VT_I32: x = (int32_t)x; break;
        case VT_U32: x = (uint32_t)x; break;
        default: break;
      }
      out.i = x;
      return true;
    }
    case 's':
      if (a.type != E7_STRING) { err = "expected a string"; return false; }
      if (!a.v.str.ptr && a.v.str.len) { err = "string pointer is NULL"; return false; }
      out.r = vm.newStringUtf8(a.v.str.len ? std::string(a.v.str.ptr, a.v.str.len) : std::string());
      return true;
    case 'y':
      if (a.type != E7_BYTES) { err = "expected bytes"; return false; }
      if (!a.v.bytes.ptr && a.v.bytes.len) { err = "bytes pointer is NULL"; return false; }
      out.r = vm.newBytesFrom(a.v.bytes.ptr, a.v.bytes.len);
      return true;
    case 'h':
      if (a.type != E7_HANDLE) { err = "expected a key handle"; return false; }
      out.r = vm.newHandle(a.v.handle);
      return true;
    default:
      err = "parameter type cannot be passed from the host";
      return false;
  }
}

static void fromSlot(e7_vm* v, char code, Slot s, e7_value* out) {
  if (!out) return;
  memset(out, 0, sizeof *out);
  switch (code) {
    case 'n': out->type = E7_NUMBER; out->v.num = s.f; break;
    case 'b': out->type = E7_BOOL; out->v.boolean = s.i != 0; break;
    case '1': case '3': case '5': case '7': out->type = E7_INT; out->v.i = s.i; break;
    case '2': case '4': case '6': case '8': out->type = E7_UINT; out->v.u = s.u; break;
    case 's': {
      Obj* o = s.r;
      if (!o || o->type != OT::String) { out->type = E7_NULL; break; }
      v->retStr = strUtf8(static_cast<StrObj*>(o));
      out->type = E7_STRING;
      out->v.str.ptr = v->retStr.c_str();
      out->v.str.len = v->retStr.size();
      break;
    }
    case 'y': {
      Obj* o = s.r;
      if (!o || o->type != OT::Bytes) { out->type = E7_NULL; break; }
      auto* b = static_cast<BytesObj*>(o);
      v->retBytes.assign(b->ptr(), b->ptr() + b->len);
      out->type = E7_BYTES;
      out->v.bytes.ptr = v->retBytes.data();
      out->v.bytes.len = v->retBytes.size();
      break;
    }
    default: out->type = E7_VOID; break;
  }
}

static e7_status finishCall(e7_vm* v, int st, e7_value* ret) {
  if (st == ERR_SUSPENDED) return E7_SUSPENDED;
  if (st == ERR_PAUSED) return E7_PAUSED;
  if (v->pendingInit) {  // a paused module initializer has finished
    v->pendingInit->initialized = true;
    v->pendingInit = nullptr;
  }
  fromSlot(v, v->pendingRet, v->vm->result(), ret);
  return E7_OK;
}

e7_status e7_call(e7_vm* v, e7_module* mod, const char* function, const e7_value* args, size_t nargs, e7_value* ret) {
  if (ret) memset(ret, 0, sizeof *ret);
  if (!v || !mod || !function) return E7_ERR_ARGS;
  if (v->active) return fail(v, E7_ERR_STATE, kReentry);
  if (v->vm->suspended()) return fail(v, E7_ERR_STATE, "VM is suspended; call e7_resume() or e7_abort_suspended()");
  const ExportDesc* ex = findExport(mod, function);
  if (!ex) return fail(v, E7_ERR_NOT_FOUND, std::string("no exported function '") + function + "'");
  const std::string& hs = ex->hostSig;
  size_t gt = hs.find('>');
  std::string params = hs.substr(0, gt);
  char rc = hs.back();
  if (rc == 'x') return fail(v, E7_ERR_ARGS, std::string("return type of '") + function + "' cannot be passed to the host");
  if (params.size() != nargs)
    return fail(v, E7_ERR_ARGS, std::string("'") + function + "' expects " + std::to_string(params.size()) + " argument(s), got " +
                                    std::to_string(nargs));
  ActiveGuard active_(v);
  try {
    std::vector<Slot> slots(nargs);
    {
      // converted arguments live only in `slots` until callEntry roots them
      VM::GCPause pause(*v->vm);
      for (size_t i = 0; i < nargs; i++) {
        std::string err;
        if (!toSlot(v, params[i], args[i], slots[i], err))
          return fail(v, E7_ERR_ARGS, "argument " + std::to_string(i + 1) + " of '" + function + "': " + err);
      }
    }
    v->pendingRet = rc;
    FuncRT* f = &mod->rt->funcs[ex->func];
    int st = v->vm->callEntry(f, slots.data(), (uint32_t)nargs);
    return finishCall(v, st, ret);
  } catch (E7Error& e) {
    return fail(v, (e7_status)e.code, e.msg);
  } catch (std::bad_alloc&) {
    v->vm->resetExecution();
    return fail(v, E7_ERR_MEMORY, "out of memory");
  }
}

e7_status e7_resume(e7_vm* v, e7_value* ret) {
  if (ret) memset(ret, 0, sizeof *ret);
  if (v->active) return fail(v, E7_ERR_STATE, kReentry);
  ActiveGuard active_(v);
  try {
    int st = v->vm->resume();
    return finishCall(v, st, ret);
  } catch (E7Error& e) {
    v->pendingInit = nullptr;
    return fail(v, (e7_status)e.code, e.msg);
  } catch (std::bad_alloc&) {
    v->pendingInit = nullptr;
    v->vm->resetExecution();
    return fail(v, E7_ERR_MEMORY, "out of memory");
  }
}

void e7_abort_suspended(e7_vm* v) {
  if (!v->active) {
    v->vm->resetExecution();
    v->pendingInit = nullptr;
  }
}
void e7_set_budget(e7_vm* v, int64_t ticks) { v->vm->setBudget(ticks); }
const char* e7_last_error(e7_vm* v) { return v->lastError.c_str(); }

static void nativeThunk(VM& vm, Slot* a, uint32_t n, Slot* r, void* ud) {
  auto* t = static_cast<NativeThunk*>(ud);
  e7_vm* v = t->owner;
  size_t gt = t->hostSig.find('>');
  std::string ps = t->hostSig.substr(0, gt);
  char rc = t->hostSig.back();
  std::vector<e7_value> args(n);
  std::vector<std::string> strs(n);
  for (uint32_t i = 0; i < n; i++) {
    e7_value& ev = args[i];
    memset(&ev, 0, sizeof ev);
    char c = ps[i];
    switch (c) {
      case 'n': ev.type = E7_NUMBER; ev.v.num = a[i].f; break;
      case 'b': ev.type = E7_BOOL; ev.v.boolean = a[i].i != 0; break;
      case '1': case '3': case '5': case '7': ev.type = E7_INT; ev.v.i = a[i].i; break;
      case '2': case '4': case '6': case '8': ev.type = E7_UINT; ev.v.u = a[i].u; break;
      case 's': {
        StrObj* s = asStr(vm, a[i]);
        strs[i] = strUtf8(s);
        ev.type = E7_STRING;
        ev.v.str.ptr = strs[i].c_str();
        ev.v.str.len = strs[i].size();
        break;
      }
      case 'y': {
        BytesObj* b = B(vm, a[i]);
        ev.type = E7_BYTES;
        ev.v.bytes.ptr = b->ptr();
        ev.v.bytes.len = b->len;
        break;
      }
      case 'h': {
        Obj* o = a[i].r;
        if (!o || o->type != OT::Handle) vm.error("expected a KeyHandle");
        ev.type = E7_HANDLE;
        ev.v.handle = static_cast<HandleObj*>(o)->id;
        break;
      }
    }
  }
  e7_value ret;
  memset(&ret, 0, sizeof ret);
  v->nativeError.clear();
  e7_status st = t->fn(v, args.data(), n, &ret, t->user);
  if (st != E7_OK) {
    std::string msg = v->nativeError.empty() ? "native function failed" : v->nativeError;
    vm.error("%s", msg.c_str());
  }
  Slot out;
  out.u = 0;
  std::string err;
  if (rc != 'v') {
    if (!toSlot(v, rc, ret, out, err)) vm.error("native returned a wrong type: %s", err.c_str());
  }
  *r = out;
}

e7_status e7_register_native(e7_vm* v, const char* name, const char* signature, e7_native_fn fn, void* user) {
  if (!v || !name || !signature || !fn) return E7_ERR_ARGS;
  std::string hs = signature;
  size_t gt = hs.find('>');
  if (gt == std::string::npos || gt + 2 != hs.size()) return fail(v, E7_ERR_ARGS, "invalid native signature");
  std::string vmSig;
  for (size_t i = 0; i < gt; i++) {
    char k = kindOfHost(hs[i]);
    if (!k || k == 'V') return fail(v, E7_ERR_ARGS, "invalid native parameter type code");
    vmSig += k;
  }
  char rk = kindOfHost(hs.back());
  if (!rk || hs.back() == 'h') return fail(v, E7_ERR_ARGS, "invalid native return type code");
  vmSig += '>';
  vmSig += rk;
  auto t = std::make_unique<NativeThunk>();
  t->owner = v;
  t->fn = fn;
  t->user = user;
  t->hostSig = hs;
  v->vm->registerNative(name, vmSig, nativeThunk, t.get());
  v->natives.push_back(std::move(t));
  return E7_OK;
}

void e7_native_error(e7_vm* v, const char* message) { v->nativeError = message ? message : ""; }

static void printTrampoline(void* ud, const char* s, size_t n) {
  auto* v = static_cast<e7_vm*>(ud);
  v->printFn(v->printUd, s, n);
}
void e7_set_print(e7_vm* v, void (*fn)(void*, const char*, size_t), void* user) {
  v->printFn = fn;
  v->printUd = user;
  v->vm->printFn = fn ? printTrampoline : nullptr;
  v->vm->printUd = v;
}
void e7_set_key_provider(e7_vm* v, int (*fn)(void*, int64_t, const uint8_t**, size_t*), void* user) {
  v->vm->hostKeyFn = fn;
  v->vm->hostKeyUd = user;
}
void e7_seed_random(e7_vm* v, uint64_t seed) { v->vm->seedRandom(seed); }
size_t e7_memory_used(e7_vm* v) { return v->vm->bytesAllocated(); }
void e7_collect_garbage(e7_vm* v) {
  if (!v->active && !v->vm->suspended()) v->vm->collectGarbage();
}

e7_status e7_gc_stats_get(e7_vm* v, e7_gc_stats* out) {
  if (!v || !out || out->struct_size < sizeof(size_t) || out->struct_size > sizeof(e7_gc_stats)) return E7_ERR_ARGS;
  e7_gc_stats st;
  memset(&st, 0, sizeof st);
  st.struct_size = out->struct_size;
  st.collections = v->vm->gcCount();
  st.total_pause_ns = v->vm->gcTotalPauseNs();
  st.max_pause_ns = v->vm->gcMaxPauseNs();
  st.heap_bytes = v->vm->bytesAllocated();
  st.peak_heap_bytes = v->vm->peakBytes();
  memcpy(out, &st, out->struct_size);
  return E7_OK;
}

e7_status e7_inspect(const uint8_t* data, size_t len, e7_module_info* info) {
  if (!data || !info || info->struct_size < sizeof(size_t) || info->struct_size > sizeof(e7_module_info))
    return E7_ERR_ARGS;
  e7_module_info r;
  memset(&r, 0, sizeof r);
  r.struct_size = info->struct_size;
  e7_status st = E7_OK;
  size_t payload = len;
  if (len >= kSigTrailer && memcmp(data + len - kSigTrailer, "E7S1", 4) == 0) {
    r.is_signed = 1;
    memcpy(r.key_id, data + len - kSigTrailer + 4, 8);
    payload = len - kSigTrailer;
  }
  ModuleHeader hdr;
  if (!readModuleHeader(data, payload, hdr)) {
    st = E7_ERR_LOAD;
  } else {
    r.format = hdr.format;
    r.requires_version = hdr.minVm;
    r.has_debug_info = (hdr.flags & kModuleFlagDebug) ? 1 : 0;
    if (hdr.format != kFormatVersion || hdr.minVm > kVmVersion) {
      st = E7_ERR_VERSION;
    } else {
      Module m;
      std::string err;
      try {
        if (!deserializeModule(data, payload, m, err)) st = E7_ERR_LOAD;
      } catch (std::bad_alloc&) {
        st = E7_ERR_MEMORY;
      }
    }
  }
  memcpy(info, &r, info->struct_size);
  return st;
}

e7_status e7_sign_module(const uint8_t* e7b, size_t len, const uint8_t seed[32], uint8_t** out, size_t* out_len) {
  if (!e7b || !seed || !out || !out_len) return E7_ERR_ARGS;
  size_t payload;
  if (checkModuleSignature(e7b, len, nullptr, 0, &payload) != 0) return E7_ERR_SIGNATURE;  // already signed
  std::vector<uint8_t> s = signModuleBytes(e7b, len, seed);
  *out = (uint8_t*)malloc(s.size());
  if (!*out) return E7_ERR_MEMORY;
  memcpy(*out, s.data(), s.size());
  *out_len = s.size();
  return E7_OK;
}
void e7_public_key(const uint8_t seed[32], uint8_t pk[32]) { ed25519PublicKey(seed, pk); }

// ---------------------------------------------------------------- debugging
static Debugger* dbgOf(e7_vm* v) { return v ? v->vm->debugger() : nullptr; }

static void fillFrame(const DebugFrame& f, e7_debug_frame& c) {
  c.function = f.function.c_str();
  c.file = f.loc.file.c_str();
  c.line = f.loc.line;
  c.column = f.loc.col;
}

static void fillStop(e7_vm* v, e7_debug_stop* out) {
  Debugger* d = dbgOf(v);
  DebugApi& a = *v->dbg;
  a.stopFrame = d->frame(0);
  a.stopMsg = d->message();
  a.stopBps = d->hitBreakpoints();
  memset(out, 0, sizeof *out);
  out->reason = (e7_debug_reason)d->reason();
  out->message = a.stopMsg.c_str();
  fillFrame(a.stopFrame, out->where);
  out->breakpoints = a.stopBps.data();
  out->num_breakpoints = a.stopBps.size();
}

e7_status e7_debug_attach(e7_vm* v, e7_debug_fn fn, void* user) {
  if (!v) return E7_ERR_ARGS;
  if (v->active || v->vm->suspended()) return fail(v, E7_ERR_STATE, "cannot attach a debugger while the VM is running or suspended");
  try {
    v->dbg.reset(new DebugApi());
    v->dbg->fn = fn;
    v->dbg->user = user;
    std::unique_ptr<Debugger> d(new Debugger(*v->vm));
    if (fn) {
      d->onStop = [v](int) -> int {
        e7_debug_stop st;
        fillStop(v, &st);
        return (int)v->dbg->fn(v, &st, v->dbg->user);
      };
    }
    v->vm->attachDebugger(std::move(d));
    return E7_OK;
  } catch (std::bad_alloc&) {
    v->vm->detachDebugger();
    v->dbg.reset();
    return fail(v, E7_ERR_MEMORY, "out of memory");
  }
}

e7_status e7_debug_detach(e7_vm* v) {
  if (!v) return E7_ERR_ARGS;
  if (v->active || v->vm->suspended()) return fail(v, E7_ERR_STATE, "cannot detach the debugger while the VM is running or suspended");
  v->vm->detachDebugger();
  v->dbg.reset();
  return E7_OK;
}

void e7_debug_break_on_error(e7_vm* v, int enable) {
  if (Debugger* d = dbgOf(v)) d->breakOnError = enable != 0;
}

int e7_debug_set_breakpoint(e7_vm* v, const char* file, uint32_t line) {
  Debugger* d = dbgOf(v);
  if (!d || !file) return 0;
  try {
    return d->addBreakpoint(file, line);
  } catch (std::bad_alloc&) {
    return 0;
  }
}

int e7_debug_breakpoint_info(e7_vm* v, int id, int* verified, uint32_t* actual_line) {
  Debugger* d = dbgOf(v);
  const Breakpoint* b = d ? d->breakpoint(id) : nullptr;
  if (verified) *verified = b && b->verified;
  if (actual_line) *actual_line = b ? (b->verified ? b->actualLine : b->line) : 0;
  return b != nullptr;
}

e7_status e7_debug_clear_breakpoint(e7_vm* v, int id) {
  Debugger* d = dbgOf(v);
  if (!d) return E7_ERR_STATE;
  return d->removeBreakpoint(id) ? E7_OK : E7_ERR_NOT_FOUND;
}

void e7_debug_clear_breakpoints(e7_vm* v) {
  if (Debugger* d = dbgOf(v)) d->clearBreakpoints();
}

void e7_debug_pause(e7_vm* v) {
  if (Debugger* d = dbgOf(v)) d->requestPause(SR_PAUSE);
}

void e7_debug_stop_on_entry(e7_vm* v) {
  if (Debugger* d = dbgOf(v)) d->requestPause(SR_ENTRY);
}

int e7_debug_is_paused(e7_vm* v) {
  Debugger* d = dbgOf(v);
  return d && d->paused();
}

e7_status e7_debug_stop_info(e7_vm* v, e7_debug_stop* out) {
  Debugger* d = dbgOf(v);
  if (!d || !d->paused() || !out) return E7_ERR_STATE;
  fillStop(v, out);
  return E7_OK;
}

size_t e7_debug_stack(e7_vm* v, const e7_debug_frame** frames) {
  if (frames) *frames = nullptr;
  Debugger* d = dbgOf(v);
  if (!d || !d->paused()) return 0;
  DebugApi& a = *v->dbg;
  size_t n = d->frameCount();
  a.frames.clear();
  for (size_t i = 0; i < n; i++) a.frames.push_back(d->frame(i));
  a.cframes.assign(n, e7_debug_frame{});
  for (size_t i = 0; i < n; i++) fillFrame(a.frames[i], a.cframes[i]);
  if (frames) *frames = a.cframes.data();
  return n;
}

size_t e7_debug_scopes(e7_vm* v, size_t frame, const e7_debug_scope** scopes) {
  if (scopes) *scopes = nullptr;
  Debugger* d = dbgOf(v);
  if (!d || !d->paused()) return 0;
  DebugApi& a = *v->dbg;
  a.scopes = d->scopes(frame);
  a.cscopes.clear();
  for (auto& s : a.scopes) a.cscopes.push_back(e7_debug_scope{s.name.c_str(), s.ref, s.count});
  if (scopes) *scopes = a.cscopes.data();
  return a.cscopes.size();
}

static void fillVar(const DebugVar& d, e7_debug_variable& c) {
  c.name = d.name.c_str();
  c.type = d.type.c_str();
  c.value = d.value.c_str();
  c.ref = d.ref;
  c.count = d.count;
}

size_t e7_debug_variables(e7_vm* v, uint32_t ref, size_t start, size_t count, const e7_debug_variable** vars) {
  if (vars) *vars = nullptr;
  Debugger* d = dbgOf(v);
  if (!d || !d->paused()) return 0;
  DebugApi& a = *v->dbg;
  a.vars = d->variables(ref, start, count);
  a.cvars.assign(a.vars.size(), e7_debug_variable{});
  for (size_t i = 0; i < a.vars.size(); i++) fillVar(a.vars[i], a.cvars[i]);
  if (vars) *vars = a.cvars.data();
  return a.cvars.size();
}

e7_status e7_debug_evaluate(e7_vm* v, size_t frame, const char* expression, const e7_debug_variable** result) {
  if (result) *result = nullptr;
  Debugger* d = dbgOf(v);
  if (!d || !d->paused()) return v ? fail(v, E7_ERR_STATE, "not paused") : E7_ERR_ARGS;
  if (!expression) return E7_ERR_ARGS;
  DebugApi& a = *v->dbg;
  std::string err;
  if (!d->evaluate(frame, expression, a.evalVar, err)) return fail(v, E7_ERR_NOT_FOUND, err);
  fillVar(a.evalVar, a.cEval);
  if (result) *result = &a.cEval;
  return E7_OK;
}

e7_status e7_debug_resume(e7_vm* v, e7_debug_action action, e7_value* ret) {
  if (ret) memset(ret, 0, sizeof *ret);
  Debugger* d = dbgOf(v);
  if (!d || !d->hostPaused()) return v ? fail(v, E7_ERR_STATE, "the debugger is not paused") : E7_ERR_ARGS;
  if (v->active) return fail(v, E7_ERR_STATE, kReentry);
  if (action == E7_DEBUG_ABORT) {
    v->vm->resetExecution();
    v->pendingInit = nullptr;
    return fail(v, E7_ERR_RUNTIME, "execution aborted by the debugger");
  }
  d->prepareResume((int)action);
  return e7_resume(v, ret);
}

}  // extern "C"
