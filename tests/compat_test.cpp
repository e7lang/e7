// Bytecode compatibility corpus (tests/compat/<version>/, see docs/BYTECODE.md "Compatibility").
//   compat_test reject <file.e7b>...   modules from an incompatible release: e7_load must fail with
//                                      E7_ERR_VERSION (never load, crash or fail differently)
//   compat_test run <file.e7b>...      modules from a compatible release: they load, and when a
//                                      <file>.out exists, running main() prints exactly that
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "e7/e7.h"

static bool readFile(const std::string& path, std::string& out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  char buf[4096];
  size_t n;
  out.clear();
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  fclose(f);
  return true;
}

static void capture(void* user, const char* s, size_t n) {
  std::string* out = static_cast<std::string*>(user);
  out->append(s, n);
  out->push_back('\n');
}

int main(int argc, char** argv) {
  if (argc < 2 || (strcmp(argv[1], "reject") != 0 && strcmp(argv[1], "run") != 0)) {
    fprintf(stderr, "usage: compat_test reject|run <file.e7b>...\n");
    return 2;
  }
  bool reject = strcmp(argv[1], "reject") == 0;
  int failures = 0;
  for (int i = 2; i < argc; i++) {
    std::string data;
    if (!readFile(argv[i], data)) {
      printf("FAIL %s: cannot read\n", argv[i]);
      failures++;
      continue;
    }
    e7_vm* vm = e7_vm_new(nullptr);
    std::string printed;
    e7_set_print(vm, capture, &printed);
    e7_set_budget(vm, 100000000);
    e7_module* m = nullptr;
    e7_status st = e7_load(vm, (const uint8_t*)data.data(), data.size(), &m);
    if (reject) {
      if (st == E7_ERR_VERSION) {
        printf("ok   %s: rejected: %s\n", argv[i], e7_last_error(vm));
      } else {
        printf("FAIL %s: expected E7_ERR_VERSION, got status %d (%s)\n", argv[i], (int)st, e7_last_error(vm));
        failures++;
      }
    } else if (st != E7_OK) {
      printf("FAIL %s: does not load (status %d): %s\n", argv[i], (int)st, e7_last_error(vm));
      failures++;
    } else {
      std::string want;
      if (readFile(std::string(argv[i]) + ".out", want)) {
        std::string lf;  // a checkout may have converted the file to CRLF line endings
        for (char c : want)
          if (c != '\r') lf.push_back(c);
        want.swap(lf);
        e7_value ret;
        if (e7_has_function(m, "main")) e7_call(vm, m, "main", nullptr, 0, &ret);
        if (printed != want) {
          printf("FAIL %s: output differs\n--- expected\n%s--- actual\n%s", argv[i], want.c_str(), printed.c_str());
          failures++;
        } else {
          printf("ok   %s: same output\n", argv[i]);
        }
      } else {
        printf("ok   %s: loads\n", argv[i]);
      }
    }
    e7_vm_free(vm);
  }
  return failures ? 1 : 0;
}
