// libFuzzer target: arbitrary bytes -> e7_load -> call main() under tight limits.
// When the first byte is odd, the module checksum is repaired so mutations reach the parser/verifier
// instead of stopping at the CRC check. The VM must never crash, hang or leak: every input ends in
// success or a clean error.
#include <cstdint>
#include <cstring>
#include <vector>

#include "e7/e7.h"

namespace e7 { uint32_t crc32(const uint8_t* p, size_t n); }

static void quiet(void*, const char*, size_t) {}

// with a debugger attached: inspect everything at each stop (debug sections are untrusted input)
static int stops = 0;
static void inspectAll(e7_vm* vm, uint32_t ref, int depth) {
  const e7_debug_variable* v;
  size_t n = e7_debug_variables(vm, ref, 0, 50, &v);
  std::vector<uint32_t> refs;
  for (size_t i = 0; i < n; i++) refs.push_back(v[i].ref);
  for (uint32_t r : refs)
    if (r && depth < 3) inspectAll(vm, r, depth + 1);
}
static e7_debug_action onStop(e7_vm* vm, const e7_debug_stop*, void*) {
  const e7_debug_frame* f;
  size_t nf = e7_debug_stack(vm, &f);
  for (size_t i = 0; i < nf && i < 4; i++) {
    const e7_debug_scope* sc;
    size_t ns = e7_debug_scopes(vm, i, &sc);
    std::vector<uint32_t> refs;
    for (size_t k = 0; k < ns; k++) refs.push_back(sc[k].ref);
    for (uint32_t r : refs) inspectAll(vm, r, 0);
  }
  return ++stops % 64 ? E7_DEBUG_STEP_IN : E7_DEBUG_CONTINUE;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1) return 0;
  bool repair = data[0] & 1;
  bool debug = data[0] & 2;  // attach a debugger
  std::vector<uint8_t> m(data + 1, data + size);
  if (repair && m.size() >= 12) {
    uint32_t c = e7::crc32(m.data(), m.size() - 8);
    for (int i = 0; i < 4; i++) m[m.size() - 4 + i] = (uint8_t)(c >> (8 * i));
  }
  e7_config cfg;
  e7_config_init(&cfg);
  cfg.memory_limit = 32u << 20;
  cfg.max_frames = 2000;
  e7_vm* vm = e7_vm_new(&cfg);
  if (!vm) return 0;
  e7_set_print(vm, quiet, nullptr);  // console.log output would only slow the fuzzer down
  e7_set_budget(vm, 200000);
  if (debug) {
    e7_debug_attach(vm, onStop, nullptr);
    e7_debug_stop_on_entry(vm);
  }
  e7_module* mod = nullptr;
  if (e7_load(vm, m.data(), m.size(), &mod) == E7_OK && e7_has_function(mod, "main")) {
    e7_value ret;
    e7_call(vm, mod, "main", nullptr, 0, &ret);
  }
  e7_vm_free(vm);
  return 0;
}
