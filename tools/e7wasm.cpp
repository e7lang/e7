// e7wasm — tiny C entry points for running E7 in a browser (WebAssembly playground).
//
//   int         e7w_run(const char* source, double budget, double memLimit, double seed)
//   const char* e7w_output(void)     everything the script printed (console.log)
//   const char* e7w_result(void)     main()'s return value rendered as text ("" if void)
//   const char* e7w_error(void)      compile / runtime error text ("" on success)
//   double      e7w_bytecode_size(void)
//   const char* e7w_disasm(const char* source)   bytecode listing (or the compile errors)
//
// The returned strings stay valid until the next e7w_* call. The same file builds natively
// (with -DE7W_NATIVE_MAIN) so it can be tested without Emscripten.
//
// Structured API used by the npm package @e7lang/e7 (packages/e7). Sizes are passed as doubles
// so that JavaScript can call every function with plain numbers; strings are UTF-8 with a length.
//
//   void        e7w_files_clear(void)                               virtual file system for imports
//   void        e7w_file_add(path, data, len)
//   int         e7w_compile(source, len, fileName)                 0 = ok, 11 = compile errors
//   const char* e7w_diagnostics(void)       JSON [{"file","line","col","message"}] (col: UTF-8 bytes)
//   uint8_t*    e7w_bytes_ptr(void) / double e7w_bytes_len(void)   bytecode of the last e7w_compile
//   const char* e7w_disasm_bytes(data, len)                       listing of a .e7b ("" + error on failure)
//   const char* e7w_exports(data, len)       JSON [{"name","sig"}] of a .e7b, or null if it does not load
//   void        e7w_args_clear(void), e7w_arg_number(x), e7w_arg_bool(b), e7w_arg_null(void),
//               e7w_arg_string(data, len), e7w_arg_bytes(data, len), e7w_arg_int(decimal, isUnsigned)
//   int         e7w_exec(data, len, call, budget, memLimit, seed)  load a .e7b, run top level, then
//               call `call` with the pushed arguments (or main() without arguments when call is "")
//   const char* e7w_output_json(void)        JSON array of printed lines of the last e7w_exec
//   const char* e7w_result_json(void)        JSON {"type":..,"value":..} of the last e7w_exec
//   int         e7w_compile_ex(source, len, fileName, flags)       e7w_compile with flags (1 = debug information)
//   const char* e7w_inspect(data, len)       JSON {"status","format","requires","signed","keyId","debug"} of a
//               .e7b header (e7_inspect); requires is major << 16 | minor << 8 | patch
//
// Debugger API (one debug session at a time; JSON protocol documented in docs/DEBUGGING.md). The VM
// runs in pause-to-host mode: every call that runs script code returns an event when the script
// stops, finishes, or (with a time slice) has used up its slice.
//
//   void        e7w_dbg_create(memLimit, seed, slice)   new session (slice > 0: return "running"
//                                                       events every `slice` instructions)
//   const char* e7w_dbg_set_breakpoints(file, lines)    replace the breakpoints of a file; lines is
//                                                       "3,17,42"; returns [{"id","line","verified"}]
//   const char* e7w_dbg_breakpoints(void)               all breakpoints [{"id","file","line","verified"}]
//                                                       (verification changes when modules load)
//   void        e7w_dbg_break_on_error(enable)
//   void        e7w_dbg_break_on_caught(enable)   also stop on errors a try/catch handler takes
//   const char* e7w_dbg_launch(data, len, call, stopOnEntry)   load a .e7b (debug information
//                                                       recommended), run its top level, then call
//                                                       `call` with the e7w_arg_* arguments ("" = main)
//   const char* e7w_dbg_resume(action)                  "continue" | "next" | "stepIn" | "stepOut"
//   void        e7w_dbg_pause(void)                     stop at the next statement (between slices)
//   const char* e7w_dbg_stack(void)                     [{"id","name","file","line","column"}]
//   const char* e7w_dbg_scopes(frame)                   [{"name","ref","count"}]
//   const char* e7w_dbg_variables(ref, start, count)    [{"name","type","value","ref","count"}]
//   const char* e7w_dbg_evaluate(frame, expression)     {"ok":true,"type","value","ref","count"} or {"ok":false,"error"}
//   const char* e7w_dbg_terminate(void)                 end the session: terminated event
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "compiler/compiler.h"
#include "core/module.h"
#include "core/numfmt.h"
#include "core/since.h"
#include "crypto/sign.h"
#include "e7/e7.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define E7W_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define E7W_EXPORT extern "C"
#endif

namespace {
std::string gOut, gResult, gError, gDisasm;
double gSize = 0;
const char* kFile = "playground.ts";

void onPrint(void* user, const char* text, size_t len) {
  std::string* s = static_cast<std::string*>(user);
  if (s->size() >= (4u << 20)) return;  // cap output at 4 MiB
  s->append(text, len);                   // one call per console.log line
  s->push_back('\n');
}

std::string render(const e7_value& v) {
  char b[64];
  switch (v.type) {
    case E7_NUMBER: return e7::numberToString(v.v.num);  // same text as String(x) in a script
    case E7_BOOL: return v.v.boolean ? "true" : "false";
    case E7_INT: snprintf(b, sizeof b, "%lld", (long long)v.v.i); return b;
    case E7_UINT: snprintf(b, sizeof b, "%llu", (unsigned long long)v.v.u); return b;
    case E7_STRING: return "\"" + std::string(v.v.str.ptr, v.v.str.len) + "\"";
    case E7_BYTES: {
      std::string s = "Uint8Array(" + std::to_string(v.v.bytes.len) + ") ";
      static const char* hx = "0123456789abcdef";
      for (size_t i = 0; i < v.v.bytes.len && i < 4096; i++) {
        s += hx[v.v.bytes.ptr[i] >> 4];
        s += hx[v.v.bytes.ptr[i] & 15];
      }
      return s;
    }
    case E7_NULL: return "null";
    default: return "";
  }
}
}  // namespace

E7W_EXPORT int e7w_run(const char* source, double budget, double memLimit, double seed) {
  gOut.clear(), gResult.clear(), gError.clear();
  gSize = 0;
  if (!source) source = "";
  size_t len = strlen(source);

  uint8_t* bc = nullptr;
  size_t bl = 0;
  char* errs = nullptr;
  e7_status st = e7_compile(source, len, kFile, &bc, &bl, &errs);
  if (st != E7_OK) {
    gError = errs ? errs : "compile error";
    e7_free(errs);
    return st;
  }
  gSize = (double)bl;

  e7_config cfg;
  e7_config_init(&cfg);
  if (memLimit > 0) cfg.memory_limit = (size_t)memLimit;
  e7_vm* vm = e7_vm_new(&cfg);
  if (!vm) {
    e7_free(bc);
    gError = "cannot create VM (out of memory)";
    return E7_ERR_MEMORY;
  }
  e7_set_print(vm, onPrint, &gOut);
  if (seed >= 0) e7_seed_random(vm, (uint64_t)seed);
  int64_t limit = budget > 0 ? (int64_t)budget : 0;
  if (limit) e7_set_budget(vm, limit);

  e7_module* mod = nullptr;
  st = e7_load(vm, bc, bl, &mod);  // runs top-level statements
  e7_free(bc);
  if (st == E7_OK && e7_has_function(mod, "main")) {
    e7_value ret;
    st = e7_call(vm, mod, "main", nullptr, 0, &ret);
    if (st == E7_OK) gResult = render(ret);
  }
  if (st != E7_OK) gError = e7_last_error(vm);
  e7_vm_free(vm);
  return st;
}

E7W_EXPORT const char* e7w_output(void) { return gOut.c_str(); }
E7W_EXPORT const char* e7w_result(void) { return gResult.c_str(); }
E7W_EXPORT const char* e7w_error(void) { return gError.c_str(); }
E7W_EXPORT double e7w_bytecode_size(void) { return gSize; }
E7W_EXPORT const char* e7w_version(void) { return e7_version(); }

E7W_EXPORT const char* e7w_disasm(const char* source) {
  std::string src = source ? source : "";
  e7::CompileOutput co = e7::compileProgram(kFile, [&](const std::string& path, std::string& content) {
    if (path != kFile) return false;  // the playground is a single file
    content = src;
    return true;
  });
  gDisasm = co.ok ? e7::disassemble(co.module) : co.errorText();
  return gDisasm.c_str();
}

// ---------------------------------------------------------------- structured API (npm package)
namespace {
std::map<std::string, std::string> gFiles;
std::string gDiag, gTmp, gOutJson, gResultJson;
std::vector<uint8_t> gBytes;
std::vector<std::string> gLines;
struct Arg {
  e7_type type;
  explicit Arg(e7_type t) : type(t) {}
  double num = 0;
  int boolean = 0;
  int64_t i = 0;
  uint64_t u = 0;
  std::string data;
};
std::vector<Arg> gArgs;

void jsonStr(std::string& o, const char* p, size_t n) {
  static const char* hx = "0123456789abcdef";
  o += '"';
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)p[i];
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          o += "\\u00";
          o += hx[c >> 4];
          o += hx[c & 15];
        } else {
          o += (char)c;
        }
    }
  }
  o += '"';
}
void jsonStr(std::string& o, const std::string& s) { jsonStr(o, s.data(), s.size()); }

void onPrintLines(void* user, const char* text, size_t len) {
  (void)user;
  if (gOut.size() >= (4u << 20)) return;  // same 4 MiB cap as e7w_run
  gOut.append(text, len);
  gOut.push_back('\n');
  gLines.emplace_back(text, len);
}

bool gCompileDebug = false;    // e7w_compile_ex flag 1

e7::CompileOutput compileWithFiles(const std::string& entry, const std::string& src) {
  e7::CompileOptions opts;
  opts.debugInfo = gCompileDebug;
  return e7::compileProgram(entry, [&](const std::string& path, std::string& content) {
    if (path == entry) {
      content = src;
      return true;
    }
    auto it = gFiles.find(path);
    if (it == gFiles.end()) return false;
    content = it->second;
    return true;
  }, opts);
}

std::string numberJson(double x) {
  if (std::isnan(x)) return "\"NaN\"";
  if (std::isinf(x)) return x > 0 ? "\"Infinity\"" : "\"-Infinity\"";
  if (x == 0 && std::signbit(x)) return "\"-0\"";
  char b[40];
  snprintf(b, sizeof b, "%.17g", x);
  return b;
}

std::string valueJson(const e7_value& v) {
  std::string o;
  char b[32];
  switch (v.type) {
    case E7_NUMBER: return "{\"type\":\"number\",\"value\":" + numberJson(v.v.num) + "}";
    case E7_BOOL: return std::string("{\"type\":\"boolean\",\"value\":") + (v.v.boolean ? "true" : "false") + "}";
    case E7_INT: snprintf(b, sizeof b, "%lld", (long long)v.v.i); return std::string("{\"type\":\"int\",\"value\":\"") + b + "\"}";
    case E7_UINT:
      snprintf(b, sizeof b, "%llu", (unsigned long long)v.v.u);
      return std::string("{\"type\":\"uint\",\"value\":\"") + b + "\"}";
    case E7_STRING:
      o = "{\"type\":\"string\",\"value\":";
      jsonStr(o, v.v.str.ptr, v.v.str.len);
      return o + "}";
    case E7_BYTES: {
      static const char* hx = "0123456789abcdef";
      o = "{\"type\":\"bytes\",\"value\":\"";
      for (size_t i = 0; i < v.v.bytes.len; i++) {
        o += hx[v.v.bytes.ptr[i] >> 4];
        o += hx[v.v.bytes.ptr[i] & 15];
      }
      return o + "\"}";
    }
    case E7_NULL: return "{\"type\":\"null\"}";
    case E7_HANDLE: snprintf(b, sizeof b, "%lld", (long long)v.v.handle); return std::string("{\"type\":\"handle\",\"value\":\"") + b + "\"}";
    default: return "{\"type\":\"void\"}";
  }
}

bool loadModule(const uint8_t* data, size_t len, e7::Module& m, std::string& err) {
  if (!data || !len) {
    err = "empty bytecode";
    return false;
  }
  size_t payload = len;
  e7::checkModuleSignature(data, len, nullptr, 0, &payload);  // a signature trailer is not part of the module
  return e7::deserializeModule(data, payload, m, err);
}
}  // namespace

E7W_EXPORT void e7w_files_clear(void) { gFiles.clear(); }
E7W_EXPORT void e7w_file_add(const char* path, const char* data, double len) {
  gFiles[path ? path : ""] = std::string(data ? data : "", data ? (size_t)len : 0);
}

E7W_EXPORT int e7w_compile(const char* source, double len, const char* fileName) {
  gBytes.clear();
  gDiag = "[]";
  std::string entry = fileName && *fileName ? fileName : "main.ts";
  try {
    e7::CompileOutput co = compileWithFiles(entry, std::string(source ? source : "", source ? (size_t)len : 0));
    if (!co.ok) {
      gDiag = "[";
      for (size_t i = 0; i < co.errors.size(); i++) {
        const auto& e = co.errors[i];
        if (i) gDiag += ',';
        gDiag += "{\"file\":";
        jsonStr(gDiag, e.file);
        gDiag += ",\"line\":" + std::to_string(e.loc.line) + ",\"col\":" + std::to_string(e.loc.col) + ",\"message\":";
        jsonStr(gDiag, e.msg);
        gDiag += '}';
      }
      gDiag += ']';
      return E7_ERR_COMPILE;
    }
    gBytes = e7::serializeModule(co.module);
    return E7_OK;
  } catch (std::bad_alloc&) {
    gDiag = "[{\"file\":";
    jsonStr(gDiag, entry);
    gDiag += ",\"line\":0,\"col\":0,\"message\":\"out of memory\"}]";
    return E7_ERR_MEMORY;
  }
}
E7W_EXPORT int e7w_compile_ex(const char* source, double len, const char* fileName, int flags) {
  gCompileDebug = (flags & 1) != 0;
  int st = e7w_compile(source, len, fileName);
  gCompileDebug = false;
  return st;
}
E7W_EXPORT const char* e7w_diagnostics(void) { return gDiag.c_str(); }
E7W_EXPORT const uint8_t* e7w_bytes_ptr(void) { return gBytes.data(); }
E7W_EXPORT double e7w_bytes_len(void) { return (double)gBytes.size(); }

E7W_EXPORT const char* e7w_disasm_bytes(const uint8_t* data, double len) {
  e7::Module m;
  std::string err;
  gTmp = loadModule(data, (size_t)len, m, err) ? e7::disassemble(m) : "";
  gError = err;
  return gTmp.c_str();
}

E7W_EXPORT const char* e7w_exports(const uint8_t* data, double len) {
  e7::Module m;
  std::string err;
  if (!loadModule(data, (size_t)len, m, err)) {
    gError = err;
    gTmp = "null";
    return gTmp.c_str();
  }
  gTmp = "[";
  for (size_t i = 0; i < m.exports.size(); i++) {
    if (i) gTmp += ',';
    gTmp += "{\"name\":";
    jsonStr(gTmp, m.exports[i].name);
    gTmp += ",\"sig\":";
    jsonStr(gTmp, m.exports[i].hostSig);
    gTmp += '}';
  }
  gTmp += ']';
  return gTmp.c_str();
}

E7W_EXPORT const char* e7w_inspect(const uint8_t* data, double len) {
  e7_module_info info;
  memset(&info, 0, sizeof info);
  info.struct_size = sizeof info;
  e7_status st = data && len > 0 ? e7_inspect(data, (size_t)len, &info) : E7_ERR_LOAD;
  static const char* hx = "0123456789abcdef";
  std::string keyId;
  for (uint8_t b : info.key_id) {
    keyId += hx[b >> 4];
    keyId += hx[b & 15];
  }
  gTmp = "{\"status\":" + std::to_string((int)st) + ",\"format\":" + std::to_string(info.format) +
         ",\"requires\":" + std::to_string(info.requires_version) + ",\"signed\":" + (info.is_signed ? "true" : "false") +
         ",\"keyId\":\"" + (info.is_signed ? keyId : "") + "\",\"debug\":" + (info.has_debug_info ? "true" : "false") + "}";
  return gTmp.c_str();
}

E7W_EXPORT void e7w_args_clear(void) { gArgs.clear(); }
E7W_EXPORT void e7w_arg_number(double x) {
  Arg a(E7_NUMBER);
  a.num = x;
  gArgs.push_back(a);
}
E7W_EXPORT void e7w_arg_bool(int b) {
  Arg a(E7_BOOL);
  a.boolean = b != 0;
  gArgs.push_back(a);
}
E7W_EXPORT void e7w_arg_null(void) { gArgs.push_back(Arg(E7_NULL)); }
E7W_EXPORT void e7w_arg_string(const char* data, double len) {
  Arg a(E7_STRING);
  a.data.assign(data ? data : "", data ? (size_t)len : 0);
  gArgs.push_back(a);
}
E7W_EXPORT void e7w_arg_bytes(const uint8_t* data, double len) {
  Arg a(E7_BYTES);
  if (data && len > 0) a.data.assign((const char*)data, (size_t)len);
  gArgs.push_back(a);
}
// 64-bit integers travel as decimal text (JavaScript numbers cannot hold every i64/u64)
E7W_EXPORT void e7w_arg_int(const char* decimal, int isUnsigned) {
  Arg a(isUnsigned ? E7_UINT : E7_INT);
  if (isUnsigned) a.u = strtoull(decimal ? decimal : "0", nullptr, 10);
  else a.i = strtoll(decimal ? decimal : "0", nullptr, 10);
  gArgs.push_back(a);
}

E7W_EXPORT int e7w_exec(const uint8_t* data, double len, const char* call, double budget, double memLimit, double seed) {
  gOut.clear(), gResult.clear(), gError.clear(), gLines.clear();
  gResultJson = "{\"type\":\"void\"}";
  gOutJson = "[]";
  gSize = len;
  e7_config cfg;
  e7_config_init(&cfg);
  if (memLimit > 0) cfg.memory_limit = (size_t)memLimit;
  e7_vm* vm = e7_vm_new(&cfg);
  if (!vm) {
    gError = "cannot create VM (out of memory)";
    return E7_ERR_MEMORY;
  }
  e7_set_print(vm, onPrintLines, nullptr);
  if (seed >= 0) e7_seed_random(vm, (uint64_t)seed);
  int64_t limit = budget > 0 ? (int64_t)budget : 0;
  if (limit) e7_set_budget(vm, limit);

  e7_module* mod = nullptr;
  e7_status st = E7_ERR_LOAD;
  if (data && len > 0) st = e7_load(vm, data, (size_t)len, &mod);  // runs top-level statements
  else gError = "empty bytecode";
  std::string fn = call ? call : "";
  bool implicitMain = fn.empty();
  if (st == E7_OK && (!implicitMain || e7_has_function(mod, "main"))) {
    if (implicitMain) fn = "main";
    std::vector<e7_value> args(implicitMain ? 0 : gArgs.size());
    for (size_t i = 0; i < args.size(); i++) {
      const Arg& a = gArgs[i];
      e7_value& v = args[i];
      memset(&v, 0, sizeof v);
      v.type = a.type;
      switch (a.type) {
        case E7_NUMBER: v.v.num = a.num; break;
        case E7_BOOL: v.v.boolean = a.boolean; break;
        case E7_INT: v.v.i = a.i; break;
        case E7_UINT: v.v.u = a.u; break;
        case E7_STRING: v.v.str.ptr = a.data.data(); v.v.str.len = a.data.size(); break;
        case E7_BYTES: v.v.bytes.ptr = (const uint8_t*)a.data.data(); v.v.bytes.len = a.data.size(); break;
        default: break;
      }
    }
    e7_value ret;
    st = e7_call(vm, mod, fn.c_str(), args.data(), args.size(), &ret);
    if (st == E7_OK) {
      gResult = render(ret);
      gResultJson = valueJson(ret);
    }
  }
  if (st != E7_OK && gError.empty()) gError = e7_last_error(vm);
  e7_vm_free(vm);
  gOutJson = "[";
  for (size_t i = 0; i < gLines.size(); i++) {
    if (i) gOutJson += ',';
    jsonStr(gOutJson, gLines[i]);
  }
  gOutJson += ']';
  return st;
}
E7W_EXPORT const char* e7w_output_json(void) { return gOutJson.c_str(); }
E7W_EXPORT const char* e7w_result_json(void) { return gResultJson.c_str(); }

// ---------------------------------------------------------------- debugger (JSON)
namespace {
struct DbgSession {
  e7_vm* vm = nullptr;
  e7_module* mod = nullptr;
  std::vector<uint8_t> bytes;
  std::string call;
  std::vector<Arg> args;
  int phase = 0;  // 0 idle, 1 top-level code, 2 call, 3 finished
  double slice = 0;
  std::map<std::string, std::vector<int>> bps;  // file -> breakpoint ids
  std::vector<std::string> out;
};
DbgSession gDbg;
std::string gDbgJson;

void dbgPrint(void*, const char* text, size_t len) {
  if (gDbg.out.size() < 100000) gDbg.out.emplace_back(text, len);
}

void frameJson(std::string& o, const e7_debug_frame& f) {
  o += "\"name\":";
  jsonStr(o, f.function ? f.function : "");
  o += ",\"file\":";
  jsonStr(o, f.file ? f.file : "");
  o += ",\"line\":" + std::to_string(f.line) + ",\"column\":" + std::to_string(f.column);
}

void varJson(std::string& o, const e7_debug_variable& v) {
  o += "\"name\":";
  jsonStr(o, v.name);
  o += ",\"type\":";
  jsonStr(o, v.type);
  o += ",\"value\":";
  jsonStr(o, v.value);
  o += ",\"ref\":" + std::to_string(v.ref) + ",\"count\":" + std::to_string(v.count);
}

void outputJson(std::string& o) {
  o += ",\"output\":[";
  for (size_t i = 0; i < gDbg.out.size(); i++) {
    if (i) o += ',';
    jsonStr(o, gDbg.out[i]);
  }
  o += ']';
  gDbg.out.clear();
}

const char* dbgEvent(e7_status st, const e7_value* ret) {
  std::string& o = gDbgJson;
  if (st == E7_PAUSED) {
    static const char* reasons[] = {"", "breakpoint", "step", "debugger", "pause", "exception", "entry"};
    e7_debug_stop s;
    e7_debug_stop_info(gDbg.vm, &s);
    o = "{\"event\":\"stopped\",\"reason\":";
    jsonStr(o, (s.reason >= 1 && s.reason <= 6) ? reasons[s.reason] : "pause");
    o += ",\"message\":";
    jsonStr(o, s.message ? s.message : "");
    o += ",\"breakpoints\":[";
    for (size_t i = 0; i < s.num_breakpoints; i++) o += (i ? "," : "") + std::to_string(s.breakpoints[i]);
    o += "],\"frame\":{";
    frameJson(o, s.where);
    o += '}';
  } else if (st == E7_SUSPENDED) {
    o = "{\"event\":\"running\"";
  } else {
    gDbg.phase = 3;
    o = "{\"event\":\"terminated\",\"status\":" + std::to_string((int)st) + ",\"result\":";
    o += (st == E7_OK && ret) ? valueJson(*ret) : std::string("{\"type\":\"void\"}");
    o += ",\"error\":";
    jsonStr(o, st == E7_OK ? std::string() : std::string(e7_last_error(gDbg.vm)));
  }
  outputJson(o);
  o += '}';
  return o.c_str();
}

// after the top-level code: call the requested function (main by default, if it exists)
const char* dbgStartCall() {
  gDbg.phase = 2;
  std::string fn = gDbg.call;
  bool implicitMain = fn.empty();
  if (implicitMain) {
    if (!e7_has_function(gDbg.mod, "main")) return dbgEvent(E7_OK, nullptr);
    fn = "main";
  }
  std::vector<e7_value> args(implicitMain ? 0 : gDbg.args.size());
  for (size_t i = 0; i < args.size(); i++) {
    const Arg& a = gDbg.args[i];
    e7_value& v = args[i];
    memset(&v, 0, sizeof v);
    v.type = a.type;
    switch (a.type) {
      case E7_NUMBER: v.v.num = a.num; break;
      case E7_BOOL: v.v.boolean = a.boolean; break;
      case E7_INT: v.v.i = a.i; break;
      case E7_UINT: v.v.u = a.u; break;
      case E7_STRING: v.v.str.ptr = a.data.data(); v.v.str.len = a.data.size(); break;
      case E7_BYTES: v.v.bytes.ptr = (const uint8_t*)a.data.data(); v.v.bytes.len = a.data.size(); break;
      default: break;
    }
  }
  if (gDbg.slice > 0) e7_set_budget(gDbg.vm, (int64_t)gDbg.slice);
  e7_value ret;
  e7_status st = e7_call(gDbg.vm, gDbg.mod, fn.c_str(), args.data(), args.size(), &ret);
  return dbgEvent(st, &ret);
}

const char* dbgAfter(e7_status st, const e7_value* ret) {
  if (st == E7_OK && gDbg.phase == 1) return dbgStartCall();
  return dbgEvent(st, ret);
}

void dbgFree() {
  if (gDbg.vm) e7_vm_free(gDbg.vm);
  gDbg = DbgSession();
}
}  // namespace

E7W_EXPORT void e7w_dbg_create(double memLimit, double seed, double slice) {
  dbgFree();
  e7_config cfg;
  e7_config_init(&cfg);
  if (memLimit > 0) cfg.memory_limit = (size_t)memLimit;
  cfg.suspend_on_budget = slice > 0;
  gDbg.vm = e7_vm_new(&cfg);
  if (!gDbg.vm) return;
  gDbg.slice = slice;
  e7_set_print(gDbg.vm, dbgPrint, nullptr);
  if (seed >= 0) e7_seed_random(gDbg.vm, (uint64_t)seed);
  e7_debug_attach(gDbg.vm, nullptr, nullptr);
}

E7W_EXPORT const char* e7w_dbg_set_breakpoints(const char* file, const char* lines) {
  gDbgJson = "[";
  if (!gDbg.vm || !file) return (gDbgJson += "]").c_str();
  for (int id : gDbg.bps[file]) e7_debug_clear_breakpoint(gDbg.vm, id);
  std::vector<int>& ids = gDbg.bps[file];
  ids.clear();
  const char* p = lines ? lines : "";
  while (*p) {
    char* end = nullptr;
    long ln = strtol(p, &end, 10);
    if (end == p) { p++; continue; }
    p = end;
    if (ln <= 0) continue;
    int id = e7_debug_set_breakpoint(gDbg.vm, file, (uint32_t)ln);
    int verified = 0;
    uint32_t actual = 0;
    e7_debug_breakpoint_info(gDbg.vm, id, &verified, &actual);
    ids.push_back(id);
    if (gDbgJson.size() > 1) gDbgJson += ',';
    gDbgJson += "{\"id\":" + std::to_string(id) + ",\"requestedLine\":" + std::to_string(ln) +
                ",\"line\":" + std::to_string(actual) + ",\"verified\":" + (verified ? "true" : "false") + "}";
  }
  gDbgJson += ']';
  return gDbgJson.c_str();
}

E7W_EXPORT const char* e7w_dbg_breakpoints(void) {
  gDbgJson = "[";
  if (gDbg.vm)
    for (auto& kv : gDbg.bps)
      for (int id : kv.second) {
        int verified = 0;
        uint32_t line = 0;
        if (!e7_debug_breakpoint_info(gDbg.vm, id, &verified, &line)) continue;
        if (gDbgJson.size() > 1) gDbgJson += ',';
        gDbgJson += "{\"id\":" + std::to_string(id) + ",\"file\":";
        jsonStr(gDbgJson, kv.first);
        gDbgJson += ",\"line\":" + std::to_string(line) + ",\"verified\":" + (verified ? "true" : "false") + "}";
      }
  gDbgJson += ']';
  return gDbgJson.c_str();
}

E7W_EXPORT void e7w_dbg_break_on_error(int enable) {
  if (gDbg.vm) e7_debug_break_on_error(gDbg.vm, enable);
}

E7W_EXPORT void e7w_dbg_break_on_caught(int enable) {
  if (gDbg.vm) e7_debug_break_on_caught_errors(gDbg.vm, enable);
}

E7W_EXPORT const char* e7w_dbg_launch(const uint8_t* data, double len, const char* call, int stopOnEntry) {
  if (!gDbg.vm) {
    gDbgJson = "{\"event\":\"terminated\",\"status\":12,\"result\":{\"type\":\"void\"},\"error\":\"no debug session (call e7w_dbg_create)\",\"output\":[]}";
    return gDbgJson.c_str();
  }
  gDbg.bytes.assign(data ? data : (const uint8_t*)"", data ? data + (size_t)len : (const uint8_t*)"");
  gDbg.call = call ? call : "";
  gDbg.args = gArgs;
  gDbg.phase = 1;
  if (stopOnEntry) e7_debug_stop_on_entry(gDbg.vm);
  e7_status st = gDbg.bytes.empty() ? E7_ERR_LOAD : e7_load(gDbg.vm, gDbg.bytes.data(), gDbg.bytes.size(), &gDbg.mod);
  if (st == E7_ERR_LOAD && gDbg.bytes.empty()) {
    gDbg.phase = 3;
    gDbgJson = "{\"event\":\"terminated\",\"status\":6,\"result\":{\"type\":\"void\"},\"error\":\"empty bytecode\",\"output\":[]}";
    return gDbgJson.c_str();
  }
  return dbgAfter(st, nullptr);
}

E7W_EXPORT const char* e7w_dbg_resume(const char* action) {
  if (!gDbg.vm || gDbg.phase == 0 || gDbg.phase == 3) {
    gDbgJson = "{\"event\":\"terminated\",\"status\":12,\"result\":{\"type\":\"void\"},\"error\":\"not running\",\"output\":[]}";
    return gDbgJson.c_str();
  }
  std::string a = action ? action : "continue";
  e7_debug_action act = a == "next" ? E7_DEBUG_STEP_OVER : a == "stepIn" ? E7_DEBUG_STEP_IN
                      : a == "stepOut" ? E7_DEBUG_STEP_OUT : E7_DEBUG_CONTINUE;
  if (gDbg.slice > 0) e7_set_budget(gDbg.vm, (int64_t)gDbg.slice);
  e7_value ret;
  e7_status st = e7_debug_is_paused(gDbg.vm) ? e7_debug_resume(gDbg.vm, act, &ret) : e7_resume(gDbg.vm, &ret);
  return dbgAfter(st, &ret);
}

E7W_EXPORT void e7w_dbg_pause(void) {
  if (gDbg.vm) e7_debug_pause(gDbg.vm);
}

E7W_EXPORT const char* e7w_dbg_stack(void) {
  gDbgJson = "[";
  const e7_debug_frame* f = nullptr;
  size_t n = gDbg.vm ? e7_debug_stack(gDbg.vm, &f) : 0;
  for (size_t i = 0; i < n; i++) {
    gDbgJson += i ? ",{" : "{";
    gDbgJson += "\"id\":" + std::to_string(i) + ",";
    frameJson(gDbgJson, f[i]);
    gDbgJson += '}';
  }
  gDbgJson += ']';
  return gDbgJson.c_str();
}

E7W_EXPORT const char* e7w_dbg_scopes(double frame) {
  gDbgJson = "[";
  const e7_debug_scope* s = nullptr;
  size_t n = gDbg.vm && frame >= 0 ? e7_debug_scopes(gDbg.vm, (size_t)frame, &s) : 0;
  for (size_t i = 0; i < n; i++) {
    gDbgJson += i ? ",{\"name\":" : "{\"name\":";
    jsonStr(gDbgJson, s[i].name);
    gDbgJson += ",\"ref\":" + std::to_string(s[i].ref) + ",\"count\":" + std::to_string(s[i].count) + "}";
  }
  gDbgJson += ']';
  return gDbgJson.c_str();
}

E7W_EXPORT const char* e7w_dbg_variables(double ref, double start, double count) {
  gDbgJson = "[";
  const e7_debug_variable* v = nullptr;
  size_t n = gDbg.vm && ref > 0 && ref < 4294967296.0
                 ? e7_debug_variables(gDbg.vm, (uint32_t)ref, start > 0 ? (size_t)start : 0, count > 0 ? (size_t)count : 0, &v)
                 : 0;
  for (size_t i = 0; i < n; i++) {
    gDbgJson += i ? ",{" : "{";
    varJson(gDbgJson, v[i]);
    gDbgJson += '}';
  }
  gDbgJson += ']';
  return gDbgJson.c_str();
}

E7W_EXPORT const char* e7w_dbg_evaluate(double frame, const char* expression) {
  const e7_debug_variable* r = nullptr;
  if (gDbg.vm && frame >= 0 && e7_debug_evaluate(gDbg.vm, (size_t)frame, expression ? expression : "", &r) == E7_OK) {
    gDbgJson = "{\"ok\":true,";
    varJson(gDbgJson, *r);
    gDbgJson += '}';
  } else {
    gDbgJson = "{\"ok\":false,\"error\":";
    jsonStr(gDbgJson, gDbg.vm ? e7_last_error(gDbg.vm) : "no debug session");
    gDbgJson += '}';
  }
  return gDbgJson.c_str();
}

E7W_EXPORT const char* e7w_dbg_terminate(void) {
  gDbgJson = "{\"event\":\"terminated\",\"status\":0,\"result\":{\"type\":\"void\"},\"error\":\"\"";
  outputJson(gDbgJson);
  gDbgJson += '}';
  dbgFree();
  return gDbgJson.c_str();
}

#ifdef E7W_NATIVE_MAIN
#include <fstream>
#include <sstream>
int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream f(argv[1], std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  if (argc > 2 && std::string(argv[2]) == "--disasm") {
    printf("%s", e7w_disasm(ss.str().c_str()));
    return 0;
  }
  if (argc > 2 && std::string(argv[2]) == "--dbg") {
    // debugger JSON API: --dbg <command>... where a command is one argument:
    //   "file PATH" (add an imported source file from disk), "bp FILE LINES", "launch [entry]",
    //   "resume ACTION", "pause", "stack", "scopes FRAME", "vars REF [START COUNT]", "eval FRAME EXPR",
    //   "terminate"; each command prints one line of JSON
    std::string src = ss.str();
    std::string entry = argv[1];
    size_t sl = entry.find_last_of('/');
    std::string dir = sl == std::string::npos ? "" : entry.substr(0, sl + 1);
    std::string name = sl == std::string::npos ? entry : entry.substr(sl + 1);
    e7w_dbg_create(64.0 * 1024 * 1024, 1, 0);
    for (int i = 3; i < argc; i++) {
      std::string c = argv[i], cmd = c, arg;
      size_t sp = c.find(' ');
      if (sp != std::string::npos) { cmd = c.substr(0, sp); arg = c.substr(sp + 1); }
      if (cmd == "file") {
        std::ifstream g(dir + arg, std::ios::binary);
        std::stringstream gs;
        gs << g.rdbuf();
        std::string d = gs.str();
        e7w_file_add(arg.c_str(), d.data(), (double)d.size());
        printf("{\"file\":\"%s\",\"bytes\":%zu}\n", arg.c_str(), d.size());
      } else if (cmd == "slice") {
        e7w_dbg_create(64.0 * 1024 * 1024, 1, atof(arg.c_str()));
        printf("{\"slice\":%s}\n", arg.c_str());
      } else if (cmd == "bp") {
        size_t s2 = arg.find(' ');
        printf("%s\n", e7w_dbg_set_breakpoints(arg.substr(0, s2).c_str(), s2 == std::string::npos ? "" : arg.substr(s2 + 1).c_str()));
      } else if (cmd == "launch") {
        int cs = e7w_compile_ex(src.c_str(), (double)src.size(), name.c_str(), 1);
        if (cs != 0) { printf("{\"compile\":%d,\"diagnostics\":%s}\n", cs, e7w_diagnostics()); return 1; }
        std::vector<uint8_t> bc(e7w_bytes_ptr(), e7w_bytes_ptr() + (size_t)e7w_bytes_len());
        size_t s2 = arg.find(' ');
        std::string fn = arg.substr(0, s2);
        bool entryStop = s2 != std::string::npos && arg.substr(s2 + 1) == "entry";
        if (fn == "entry") { fn.clear(); entryStop = true; }
        printf("%s\n", e7w_dbg_launch(bc.data(), (double)bc.size(), fn.c_str(), entryStop));
      } else if (cmd == "resume") {
        printf("%s\n", e7w_dbg_resume(arg.c_str()));
      } else if (cmd == "bps") {
        printf("%s\n", e7w_dbg_breakpoints());
      } else if (cmd == "pause") {
        e7w_dbg_pause();
        printf("{}\n");
      } else if (cmd == "stack") {
        printf("%s\n", e7w_dbg_stack());
      } else if (cmd == "scopes") {
        printf("%s\n", e7w_dbg_scopes(atof(arg.c_str())));
      } else if (cmd == "vars") {
        double r = 0, s0 = 0, n0 = 0;
        sscanf(arg.c_str(), "%lf %lf %lf", &r, &s0, &n0);
        printf("%s\n", e7w_dbg_variables(r, s0, n0));
      } else if (cmd == "eval") {
        size_t s2 = arg.find(' ');
        printf("%s\n", e7w_dbg_evaluate(atof(arg.substr(0, s2).c_str()), s2 == std::string::npos ? "" : arg.substr(s2 + 1).c_str()));
      } else if (cmd == "terminate") {
        printf("%s\n", e7w_dbg_terminate());
      } else {
        printf("{\"error\":\"unknown command\"}\n");
      }
    }
    return 0;
  }
  if (argc > 2 && std::string(argv[2]) == "--json") {  // structured API: --json [fn [string args...]]
    std::string src = ss.str();
    int cs = e7w_compile(src.c_str(), (double)src.size(), argv[1]);
    printf("compile=%d diagnostics=%s\n", cs, e7w_diagnostics());
    if (cs != 0) return 0;
    std::vector<uint8_t> bc(e7w_bytes_ptr(), e7w_bytes_ptr() + (size_t)e7w_bytes_len());
    printf("exports=%s\n", e7w_exports(bc.data(), (double)bc.size()));
    printf("inspect=%s\n", e7w_inspect(bc.data(), (double)bc.size()));
    e7w_args_clear();
    for (int i = 4; i < argc; i++) e7w_arg_string(argv[i], (double)strlen(argv[i]));
    int st = e7w_exec(bc.data(), (double)bc.size(), argc > 3 ? argv[3] : "", 1e9, 64.0 * 1024 * 1024, 1);
    printf("status=%d output=%s result=%s error=%s\n", st, e7w_output_json(), e7w_result_json(), e7w_error());
    return 0;
  }
  int st = e7w_run(ss.str().c_str(), 1e9, 64.0 * 1024 * 1024, 1);
  printf("status=%d bytes=%.0f\n--- output\n%s--- result\n%s\n--- error\n%s\n", st, e7w_bytecode_size(), e7w_output(),
         e7w_result(), e7w_error());
  return 0;
}
#endif
