// Regression test: hand-crafted malicious .e7b modules (from a security review) must be rejected
// or fail with a script error — never crash or corrupt memory (run under ASan in CI).
// Files named run_*.e7b only have to run without crashing (any result is fine).
// Usage: malicious_test <file.e7b>...
#include <cstdio>
#include <cstring>
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

// second pass: with a debugger attached that steps through everything and inspects every variable
static int stops = 0;
static void inspectAll(e7_vm* vm, uint32_t ref, int depth) {
  const e7_debug_variable* v;
  size_t n = e7_debug_variables(vm, ref, 0, 100, &v);
  std::vector<uint32_t> refs;
  for (size_t i = 0; i < n; i++) refs.push_back(v[i].ref);
  for (uint32_t r : refs)
    if (r && depth < 4) inspectAll(vm, r, depth + 1);
}
static e7_debug_action onStop(e7_vm* vm, const e7_debug_stop*, void*) {
  const e7_debug_frame* f;
  size_t nf = e7_debug_stack(vm, &f);
  for (size_t i = 0; i < nf && i < 8; i++) {
    const e7_debug_scope* sc;
    size_t ns = e7_debug_scopes(vm, i, &sc);
    std::vector<uint32_t> refs;
    for (size_t k = 0; k < ns; k++) refs.push_back(sc[k].ref);
    for (uint32_t r : refs) inspectAll(vm, r, 0);
  }
  return ++stops % 500 ? E7_DEBUG_STEP_IN : E7_DEBUG_CONTINUE;
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
    for (int pass = 0; pass < 2; pass++) {
      e7_vm* vm = e7_vm_new(&cfg);
      e7_set_print(vm, [](void*, const char*, size_t) {}, nullptr);
      e7_set_budget(vm, 10000000);
      if (pass == 1) {
        e7_debug_attach(vm, onStop, nullptr);
        e7_debug_stop_on_entry(vm);
      }
      e7_module* m = nullptr;
      e7_status st = e7_load(vm, data.data(), data.size(), &m);
      const char* stage = "load";
      if (st == E7_OK && e7_has_function(m, "main")) {
        e7_value ret;
        st = e7_call(vm, m, "main", nullptr, 0, &ret);
        stage = "call";
      }
      const char* base = strrchr(argv[i], '/');
      base = base ? base + 1 : argv[i];
      const char* bs = strrchr(base, '\\');
      if (bs) base = bs + 1;
      if (strncmp(base, "run_", 4) == 0) {
        printf("ok   %s: ran without crashing (status %d)%s\n", argv[i], (int)st, pass ? " (debugger attached)" : "");
      } else if (st == E7_OK) {
        printf("FAIL %s: accepted and ran successfully\n", argv[i]);
        failures++;
      } else {
        printf("ok   %s: rejected at %s: %s%s\n", argv[i], stage, e7_last_error(vm), pass ? " (debugger attached)" : "");
      }
      e7_vm_free(vm);
    }
  }
  return failures ? 1 : 0;
}
