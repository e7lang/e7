// Regression tests (from a security review) for host functions that call back into the C API, and
// for GC pressure while the C API converts host arguments. Run under ASan in CI.
#include <cstdio>
#include <cstring>
#include <string>

#include "e7/e7.h"

static int failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      failures++;                                                     \
    }                                                                 \
  } while (0)

static e7_module* gmod;
static int mode;
static e7_status innerStatus;

static e7_status hostN(e7_vm* vm, const e7_value*, size_t, e7_value* ret, void*) {
  if (mode == 1) {
    e7_value r;
    innerStatus = e7_call(vm, gmod, "str", nullptr, 0, &r);  // must be refused, not corrupt the outer call
  } else if (mode == 2) {
    e7_collect_garbage(vm);  // must be ignored while script code runs
  } else if (mode == 4) {
    e7_abort_suspended(vm);  // must be ignored while script code runs
  }
  ret->type = E7_NUMBER;
  ret->v.num = 1;
  return E7_OK;
}

static const char* src = R"(
declare function host(): number;
export function num(): number { return host() + 41.5; }
export function str(): string { return "s"; }
class P { a: string = ""; constructor(s: string) { this.a = s; } }
export function sortGc(): number {
  const arr: P[] = [];
  for (let i = 0; i < 50; i++) arr.push(new P("v" + i));
  let first = true;
  arr.sort((x: P, y: P): number => {
    if (first) { first = false; arr.length = 0; host(); }
    return x.a.length - y.a.length + (x.a < y.a ? -1 : 1);
  });
  return arr.length;
}
let keep: string[] = [];
export function fill(n: number): number { keep.push("x".repeat(n)); return keep.length; }
export function two(a: string, b: string): number { return a.length + b.length + a.charCodeAt(0); }
)";

static e7_vm* setup(int m, size_t limit) {
  mode = m;
  e7_config cfg;
  e7_config_init(&cfg);
  if (limit) cfg.memory_limit = limit;
  e7_vm* vm = e7_vm_new(&cfg);
  e7_register_native(vm, "host", ">n", hostN, nullptr);
  if (e7_load_source(vm, src, strlen(src), "reentry.ts", &gmod) != E7_OK) {
    printf("load failed: %s\n", e7_last_error(vm));
    failures++;
  }
  return vm;
}

int main() {
  e7_value r;
  {
    e7_vm* vm = setup(1, 0);
    e7_status st = e7_call(vm, gmod, "num", nullptr, 0, &r);
    CHECK(innerStatus == E7_ERR_STATE);
    CHECK(st == E7_OK && r.type == E7_NUMBER && r.v.num == 42.5);
    e7_vm_free(vm);
  }
  {
    e7_vm* vm = setup(2, 0);
    e7_status st = e7_call(vm, gmod, "sortGc", nullptr, 0, &r);
    CHECK(st == E7_OK);
    e7_vm_free(vm);
  }
  {
    e7_vm* vm = setup(3, 4u << 20);
    e7_value a;
    a.type = E7_NUMBER;
    a.v.num = 1500000;  // keeps a ~3 MB string alive
    CHECK(e7_call(vm, gmod, "fill", &a, 1, &r) == E7_OK);
    std::string s1(300000, 'A'), s2(300000, 'B');
    e7_value args[2];
    args[0].type = E7_STRING; args[0].v.str.ptr = s1.data(); args[0].v.str.len = s1.size();
    args[1].type = E7_STRING; args[1].v.str.ptr = s2.data(); args[1].v.str.len = s2.size();
    e7_status st = e7_call(vm, gmod, "two", args, 2, &r);
    CHECK(st == E7_ERR_MEMORY || (st == E7_OK && r.v.num == 600065));
    // the VM stays usable afterwards
    CHECK(e7_call(vm, gmod, "str", nullptr, 0, &r) == E7_OK);
    e7_vm_free(vm);
  }
  {
    e7_vm* vm = setup(4, 0);
    e7_status st = e7_call(vm, gmod, "num", nullptr, 0, &r);
    CHECK(st == E7_OK && r.v.num == 42.5);
    e7_vm_free(vm);
  }
  if (failures) return 1;
  printf("capi_reentry_test: all checks passed\n");
  return 0;
}
