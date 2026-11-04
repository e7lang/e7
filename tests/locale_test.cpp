// Number parsing/formatting must not depend on the host's C locale (e.g. ',' as decimal separator).
#include <clocale>
#include <cstdio>
#include <cstring>
#include <string>

#include "e7/e7.h"

int main() {
  const char* locales[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "German_Germany.1252", "de-DE"};
  const char* used = nullptr;
  for (const char* l : locales)
    if (setlocale(LC_ALL, l)) { used = l; break; }
  if (!used) {
    printf("locale_test: no comma-decimal locale available, skipped\n");
    return 0;
  }
  const char* src =
      "export function f(): string { const x = 3.14; return String(x) + ' ' + (0.1).toString() + ' ' + "
      "parseFloat('2.5') + ' ' + (2.345).toFixed(1) + ' ' + JSON.stringify(JSON.parse('[1.5,-0.25e1]') as number[]) + ' ' + "
      "Number(10n ** 30n) + ' ' + (1.5e-7).toPrecision(3) + ' ' + Number('6.02e23') + ' ' + (0.5).toString(2); }";
  e7_vm* vm = e7_vm_new(nullptr);
  e7_module* m;
  if (e7_load_source(vm, src, strlen(src), "l.ts", &m) != E7_OK) {
    printf("locale_test: %s\n", e7_last_error(vm));
    return 1;
  }
  e7_value r;
  if (e7_call(vm, m, "f", nullptr, 0, &r) != E7_OK) { printf("locale_test: %s\n", e7_last_error(vm)); return 1; }
  std::string got(r.v.str.ptr, r.v.str.len);
  const char* want = "3.14 0.1 2.5 2.3 [1.5,-2.5] 1e+30 1.50e-7 6.02e+23 0.1";
  e7_vm_free(vm);
  if (got != want) {
    printf("locale_test FAILED under %s:\n  got  %s\n  want %s\n", used, got.c_str(), want);
    return 1;
  }
  printf("locale_test: passed under %s\n", used);
  return 0;
}
