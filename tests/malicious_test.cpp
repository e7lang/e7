// Regression test: hand-crafted malicious .e7b modules (from a security review) must be rejected
// or fail with a script error — never crash or corrupt memory (run under ASan in CI).
// Usage: malicious_test <file.e7b>...
#include <cstdio>
#include <vector>

#include "e7/e7.h"

static bool readFile(const char* path, std::vector<unsigned char>& out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  unsigned char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
  fclose(f);
  return true;
}

int main(int argc, char** argv) {
  int failures = 0;
  for (int i = 1; i < argc; i++) {
    std::vector<unsigned char> data;
    if (!readFile(argv[i], data)) {
      printf("FAIL %s: cannot read\n", argv[i]);
      failures++;
      continue;
    }
    e7_config cfg;
    e7_config_init(&cfg);
    cfg.memory_limit = 64 << 20;
    e7_vm* vm = e7_vm_new(&cfg);
    e7_set_print(vm, [](void*, const char*, size_t) {}, nullptr);
    e7_set_budget(vm, 10000000);
    e7_module* m = nullptr;
    e7_status st = e7_load(vm, data.data(), data.size(), &m);
    const char* stage = "load";
    if (st == E7_OK && e7_has_function(m, "main")) {
      e7_value ret;
      st = e7_call(vm, m, "main", nullptr, 0, &ret);
      stage = "call";
    }
    if (st == E7_OK) {
      printf("FAIL %s: accepted and ran successfully\n", argv[i]);
      failures++;
    } else {
      printf("ok   %s: rejected at %s: %s\n", argv[i], stage, e7_last_error(vm));
    }
    e7_vm_free(vm);
  }
  return failures ? 1 : 0;
}
