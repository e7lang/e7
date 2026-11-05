// e7 — run E7 programs (source or bytecode).
#include <chrono>
#include <cstring>

#include "common.h"
#include "diag.h"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "compiler/compiler.h"
#include "core/module.h"
#include "e7/e7.h"

int runTestCommand(int argc, char** argv);   // e7_test.cpp
int runDebugCommand(int argc, char** argv);  // e7_debug.cpp

static int usage() {
  fprintf(stderr,
          "usage: e7 [options] <file.ts|file.e7|file.e7b>\n"
          "       e7 test [options] [files or directories...]   (run tests; see e7 test --help)\n"
          "       e7 debug [--break file:line]... [--call fn] <file>   (command-line debugger)\n"
          "  --call <fn>        call an exported function (no arguments) and print its result\n"
          "                     (default: main, if exported)\n"
          "  --budget <n>       instruction budget (ticks)\n"
          "  --slice <n>        run in suspendable slices of n ticks (demonstrates e7_resume)\n"
          "  --mem <bytes>      heap limit\n"
          "  --seed <n>         seed for Math.random\n"
          "  --trust <pub-hex-file>  require a module signature from this key\n"
          "  --time             print execution time\n"
          "  --no-excerpt       compile errors without source excerpts\n"
          "  --version          print the version\n");
  return 2;
}

static void printValue(const e7_value& v) {
  switch (v.type) {
    case E7_NUMBER: {
      char b[64];
      snprintf(b, sizeof b, "%.17g", v.v.num);
      printf("%s\n", b);
      break;
    }
    case E7_BOOL: printf("%s\n", v.v.boolean ? "true" : "false"); break;
    case E7_INT: printf("%lld\n", (long long)v.v.i); break;
    case E7_UINT: printf("%llu\n", (unsigned long long)v.v.u); break;
    case E7_STRING: printf("%.*s\n", (int)v.v.str.len, v.v.str.ptr); break;
    case E7_BYTES: {
      for (size_t i = 0; i < v.v.bytes.len; i++) printf("%02x", v.v.bytes.ptr[i]);
      printf("\n");
      break;
    }
    default: break;
  }
}

int main(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  if (argc >= 2 && strcmp(argv[1], "test") == 0) return runTestCommand(argc - 2, argv + 2);
  if (argc >= 2 && strcmp(argv[1], "debug") == 0) return runDebugCommand(argc - 2, argv + 2);
  std::string file, call, trust;
  int64_t budget = 0, slice = 0;
  size_t mem = 0;
  uint64_t seed = 0;
  bool hasSeed = false, timeIt = false, excerpt = true;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--call" && i + 1 < argc) call = argv[++i];
    else if (a == "--budget" && i + 1 < argc) budget = atoll(argv[++i]);
    else if (a == "--slice" && i + 1 < argc) slice = atoll(argv[++i]);
    else if (a == "--mem" && i + 1 < argc) mem = (size_t)atoll(argv[++i]);
    else if (a == "--seed" && i + 1 < argc) { seed = strtoull(argv[++i], nullptr, 10); hasSeed = true; }
    else if (a == "--trust" && i + 1 < argc) trust = argv[++i];
    else if (a == "--time") timeIt = true;
    else if (a == "--no-excerpt") excerpt = false;
    else if (a == "--version" || a == "-v") { printf("e7 %s\n", E7_VERSION); return 0; }
    else if (a.size() && a[0] == '-') return usage();
    else file = a;
  }
  if (file.empty()) return usage();
  e7_config cfg;
  e7_config_init(&cfg);
  if (mem) cfg.memory_limit = mem;
  std::vector<uint8_t> key;
  if (!trust.empty()) {
    std::string hex;
    if (!readAll(trust, hex) || !parseHex(hex, key) || key.size() != 32) {
      fprintf(stderr, "e7: invalid public key file\n");
      return 1;
    }
    cfg.require_signature = 1;
    cfg.trusted_keys = key.data();
    cfg.num_trusted_keys = 1;
  }
  if (slice) cfg.suspend_on_budget = 1;
  e7_vm* vm = e7_vm_new(&cfg);
  if (!vm) { fprintf(stderr, "e7: cannot create VM\n"); return 1; }
  if (hasSeed) e7_seed_random(vm, seed);
  std::vector<uint8_t> bytes;
  if (endsWith(file, ".e7b")) {
    std::string data;
    if (!readAll(file, data)) { fprintf(stderr, "e7: cannot read %s\n", file.c_str()); e7_vm_free(vm); return 1; }
    bytes.assign(data.begin(), data.end());
  } else {
    e7::CompileOptions copts;
    copts.debugInfo = true;  // running from source: column numbers in stack traces
    e7::CompileOutput co = e7::compileProgram(file, e7::readFileFromDisk, copts);
    if (!co.ok) {
      fprintf(stderr, "%s", formatCompileErrors(co, excerpt).c_str());
      e7_vm_free(vm);
      return 1;
    }
    bytes = e7::serializeModule(co.module);
    if (!trust.empty()) { fprintf(stderr, "e7: --trust requires a signed .e7b file\n"); e7_vm_free(vm); return 1; }
  }
  auto t0 = std::chrono::steady_clock::now();
  if (budget) e7_set_budget(vm, budget);
  e7_module* mod = nullptr;
  e7_status st = e7_load(vm, bytes.data(), bytes.size(), &mod);
  if (st != E7_OK) {
    fprintf(stderr, "%s\n", e7_last_error(vm));
    e7_vm_free(vm);
    return st == E7_ERR_RUNTIME ? 3 : 1;
  }
  if (call.empty() && e7_has_function(mod, "main")) call = "main";
  int rc = 0;
  if (!call.empty()) {
    e7_value ret;
    if (slice) e7_set_budget(vm, slice);
    st = e7_call(vm, mod, call.c_str(), nullptr, 0, &ret);
    int slices = 1;
    while (st == E7_SUSPENDED) {
      e7_set_budget(vm, slice);
      st = e7_resume(vm, &ret);
      slices++;
    }
    if (slice) fprintf(stderr, "[ran in %d slices]\n", slices);
    if (st != E7_OK) {
      fprintf(stderr, "%s\n", e7_last_error(vm));
      rc = st == E7_ERR_RUNTIME ? 3 : 1;
    } else if (ret.type != E7_VOID) {
      printValue(ret);
    }
  }
  if (timeIt) {
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    fprintf(stderr, "[time %.3f ms, heap %zu bytes]\n", ms, e7_memory_used(vm));
  }
  e7_vm_free(vm);
  return rc;
}
