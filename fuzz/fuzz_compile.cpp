// libFuzzer target: arbitrary text -> compiler. Programs that compile must load (pass the verifier)
// and run under tight limits without crashing. A compiler self-check failure ("internal compiler
// error", reported at location 0:0) or a compiled module that the loader rejects is a bug and aborts.
// The check uses the structured diagnostics, not the message text, because user input is echoed in
// ordinary diagnostics (e.g. unknown module names) and the fuzzer quickly learns to copy strings.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "compiler/compiler.h"
#include "core/module.h"
#include "e7/e7.h"

static void quiet(void*, const char*, size_t) {}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 64 * 1024) return 0;  // keep inputs small; the compiler is not the bottleneck being tested
  std::vector<uint8_t> bytes;
  try {
    e7::CompileOutput co = e7::compileSource("fuzz.ts", std::string((const char*)data, size));
    if (!co.ok) {
      for (const auto& e : co.errors)
        if (e.loc.line == 0 && e.loc.col == 0 && e.msg.rfind("internal compiler error", 0) == 0) {
          fprintf(stderr, "%s\n", co.errorText().c_str());
          abort();
        }
      return 0;
    }
    bytes = e7::serializeModule(co.module);
  } catch (const std::bad_alloc&) {
    return 0;
  }
  e7_config cfg;
  e7_config_init(&cfg);
  cfg.memory_limit = 32u << 20;
  cfg.max_frames = 2000;
  e7_vm* vm = e7_vm_new(&cfg);
  if (!vm) return 0;
  e7_set_print(vm, quiet, nullptr);  // console.log output would only slow the fuzzer down
  e7_set_budget(vm, 200000);
  e7_module* mod = nullptr;
  e7_status ls = e7_load(vm, bytes.data(), bytes.size(), &mod);
  if (ls == E7_ERR_LOAD || ls == E7_ERR_VERIFY) {
    fprintf(stderr, "compiler produced a module the loader rejects: %s\n", e7_last_error(vm));
    abort();
  }
  if (ls == E7_OK && e7_has_function(mod, "main")) {
    e7_value ret;
    e7_call(vm, mod, "main", nullptr, 0, &ret);
  }
  e7_vm_free(vm);
  return 0;
}
