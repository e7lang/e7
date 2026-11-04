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

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1) return 0;
  bool repair = data[0] & 1;
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
  e7_module* mod = nullptr;
  if (e7_load(vm, m.data(), m.size(), &mod) == E7_OK && e7_has_function(mod, "main")) {
    e7_value ret;
    e7_call(vm, mod, "main", nullptr, 0, &ret);
  }
  e7_vm_free(vm);
  return 0;
}
