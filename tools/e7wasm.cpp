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

e7::CompileOutput compileWithFiles(const std::string& entry, const std::string& src) {
  return e7::compileProgram(entry, [&](const std::string& path, std::string& content) {
    if (path == entry) {
      content = src;
      return true;
    }
    auto it = gFiles.find(path);
    if (it == gFiles.end()) return false;
    content = it->second;
    return true;
  });
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
  if (argc > 2 && std::string(argv[2]) == "--json") {  // structured API: --json [fn [string args...]]
    std::string src = ss.str();
    int cs = e7w_compile(src.c_str(), (double)src.size(), argv[1]);
    printf("compile=%d diagnostics=%s\n", cs, e7w_diagnostics());
    if (cs != 0) return 0;
    std::vector<uint8_t> bc(e7w_bytes_ptr(), e7w_bytes_ptr() + (size_t)e7w_bytes_len());
    printf("exports=%s\n", e7w_exports(bc.data(), (double)bc.size()));
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
