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
#include <cstdio>
#include <cstring>
#include <string>

#include "compiler/compiler.h"
#include "core/module.h"
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
    case E7_NUMBER: {
      // shortest round-trip form, like JavaScript's Number#toString for common values
      for (int p = 1; p <= 17; p++) {
        snprintf(b, sizeof b, "%.*g", p, v.v.num);
        if (strtod(b, nullptr) == v.v.num) break;
      }
      return b;
    }
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
  int st = e7w_run(ss.str().c_str(), 1e9, 64.0 * 1024 * 1024, 1);
  printf("status=%d bytes=%.0f\n--- output\n%s--- result\n%s\n--- error\n%s\n", st, e7w_bytecode_size(), e7w_output(),
         e7w_result(), e7w_error());
  return 0;
}
#endif
