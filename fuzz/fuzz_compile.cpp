// libFuzzer target: arbitrary text -> e7_compile. Programs that compile must load (pass the verifier)
// and run under tight limits without crashing. An "internal compiler error" or a compiled module that
// the verifier rejects is a bug and aborts.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "e7/e7.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 64 * 1024) return 0;  // keep inputs small; the compiler is not the bottleneck being tested
  uint8_t* out = nullptr;
  size_t len = 0;
  char* errs = nullptr;
  e7_status st = e7_compile((const char*)data, size, "fuzz.ts", &out, &len, &errs);
  if (st != E7_OK) {
    if (errs && strstr(errs, "internal compiler error")) {
      fprintf(stderr, "%s\n", errs);
      abort();
    }
    e7_free(errs);
    return 0;
  }
  e7_config cfg;
  e7_config_init(&cfg);
  cfg.memory_limit = 32u << 20;
  cfg.max_frames = 2000;
  e7_vm* vm = e7_vm_new(&cfg);
  if (vm) {
    e7_set_budget(vm, 200000);
    e7_module* mod = nullptr;
    e7_status ls = e7_load(vm, out, len, &mod);
    if (ls == E7_ERR_LOAD || ls == E7_ERR_VERIFY) {
      fprintf(stderr, "compiler produced a module the loader rejects: %s\n", e7_last_error(vm));
      abort();
    }
    if (ls == E7_OK && e7_has_function(mod, "main")) {
      e7_value ret;
      e7_call(vm, mod, "main", nullptr, 0, &ret);
    }
    e7_vm_free(vm);
  }
  e7_free(out);
  return 0;
}
