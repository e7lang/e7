// GC pause benchmark through the C API. For each size, a script builds a linked list of N nodes (a
// class holding a string, a number array and the next node: 3 heap objects per node), then the host
// times full collections (e7_collect_garbage) with the whole structure live, and after the script
// dropped it (the collection then frees everything). Reports e7_gc_stats_get as well (collections
// triggered while building, longest pause).
//
//   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DE7_BUILD_BENCH=ON && cmake --build build --target e7_gc_pause
//   build/e7_gc_pause [nodes...]        (default: 10000 100000 300000 1000000)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "e7/e7.h"

static const char kSource[] = R"E7(
class Node {
  constructor(public name: string, public data: number[], public next: Node | null) {}
}
let head: Node | null = null;
export function build(n: number): number {
  let h: Node | null = null;
  for (let i = 0; i < n; i++) h = new Node("node-" + i, [i, i + 1, i + 2], h);
  head = h;
  return n;
}
export function count(): number {
  let c = 0;
  for (let p = head; p !== null; p = p.next) c++;
  return c;
}
export function drop(): void { head = null; }
)E7";

static double msSince(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

static e7_gc_stats stats(e7_vm* vm) {
  e7_gc_stats s;
  memset(&s, 0, sizeof s);
  s.struct_size = sizeof s;
  e7_gc_stats_get(vm, &s);
  return s;
}

static bool call(e7_vm* vm, e7_module* m, const char* fn, double arg, e7_value* ret) {
  e7_value a;
  a.type = E7_NUMBER;
  a.v.num = arg;
  e7_status st = e7_call(vm, m, fn, &a, strcmp(fn, "build") == 0 ? 1 : 0, ret);
  if (st != E7_OK) fprintf(stderr, "e7_gc_pause: %s: %s\n", fn, e7_last_error(vm));
  return st == E7_OK;
}

int main(int argc, char** argv) {
  std::vector<long> sizes;
  for (int i = 1; i < argc; i++) sizes.push_back(atol(argv[i]));
  if (sizes.empty()) sizes = {10000, 100000, 300000, 1000000};
  printf("e7 %s GC pause benchmark (3 heap objects per node)\n\n", e7_version());
  printf("| nodes | heap objects | live heap | build | auto GCs (max pause) | full GC, all live | full GC after drop |\n");
  printf("|---:|---:|---:|---:|---:|---:|---:|\n");
  for (long n : sizes) {
    e7_config cfg;
    e7_config_init(&cfg);
    cfg.memory_limit = (size_t)1 << (sizeof(size_t) > 4 ? 32 : 31);
    e7_vm* vm = e7_vm_new(&cfg);
    e7_module* m = nullptr;
    if (!vm || e7_load_source(vm, kSource, sizeof kSource - 1, "gc_pause.ts", &m) != E7_OK) {
      fprintf(stderr, "e7_gc_pause: %s\n", vm ? e7_last_error(vm) : "cannot create VM");
      return 1;
    }
    e7_value r;
    auto t0 = std::chrono::steady_clock::now();
    if (!call(vm, m, "build", (double)n, &r)) return 1;
    double buildMs = msSince(t0);
    e7_gc_stats during = stats(vm);

    // everything live: the collection marks every object and frees nothing (best of 3)
    double liveMs = 1e30;
    for (int k = 0; k < 3; k++) {
      t0 = std::chrono::steady_clock::now();
      e7_collect_garbage(vm);
      double ms = msSince(t0);
      if (ms < liveMs) liveMs = ms;
    }
    size_t liveBytes = e7_memory_used(vm);
    if (!call(vm, m, "count", 0, &r) || r.v.num != (double)n) {
      fprintf(stderr, "e7_gc_pause: the list lost nodes\n");
      return 1;
    }
    // dropped: the collection sweeps (frees) every node
    if (!call(vm, m, "drop", 0, &r)) return 1;
    t0 = std::chrono::steady_clock::now();
    e7_collect_garbage(vm);
    double dropMs = msSince(t0);
    e7_gc_stats after = stats(vm);

    printf("| %ld | %ld | %.1f MB | %.1f ms | %llu (%.2f ms) | %.2f ms | %.2f ms |\n", n, 3 * n, liveBytes / 1048576.0,
           buildMs, (unsigned long long)during.collections, during.max_pause_ns / 1e6, liveMs, dropMs);
    fflush(stdout);
    if (after.heap_bytes > liveBytes / 10) {
      fprintf(stderr, "e7_gc_pause: %zu bytes still allocated after dropping the list\n", after.heap_bytes);
      return 1;
    }
    e7_vm_free(vm);
  }
  printf("\nheap objects: Node + string + array per node; live heap: e7_memory_used() with the list alive;\n"
         "auto GCs: collections the VM ran while building (e7_gc_stats_get), with the longest pause;\n"
         "full GC: e7_collect_garbage() wall time (all live: best of 3).\n");
  return 0;
}
