// Soak test: one VM, many calls of a mix of exported functions
// (strings, arrays, maps, closures, classes, try/catch, crypto, JSON, BigInt, a host native; half of
// them with string / bytes arguments converted by the C API, some in suspended slices). After a
// warm-up, the heap after a full collection must stay flat: every sample equals the first one within
// a small tolerance and the second half of the samples is not above the first half (no growth
// trend); the peak heap stays bounded and VM::checkHeap holds at every sample. Every 1000 calls a
// module whose initializer fails is loaded too (nothing of it may stay allocated).
//
// usage: soak_test [--calls N] [--samples K] [-v]     (default 100000 calls, 20 samples;
//                                                      E7_GC_STRESS builds: 3000 calls)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "capi/testing.h"
#include "e7/e7.h"
#include "vm/vm.h"
#include "workload.h"

// Bytes of the process's malloc heap in use (C++ objects of the VM and the C API outside the script
// heap: a leak there does not show in e7_memory_used). 0 where unavailable or under a sanitizer.
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define E7_SOAK_NO_MALLOC_STATS 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define E7_SOAK_NO_MALLOC_STATS 1
#endif
#if !defined(E7_SOAK_NO_MALLOC_STATS) && defined(__APPLE__)
#include <malloc/malloc.h>
static size_t processHeapInUse() {
  malloc_statistics_t st;
  malloc_zone_statistics(nullptr, &st);
  return st.size_in_use;
}
#elif !defined(E7_SOAK_NO_MALLOC_STATS) && defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
#include <malloc.h>
static size_t processHeapInUse() { return mallinfo2().uordblks; }
#else
static size_t processHeapInUse() { return 0; }
#endif

static const char* kExtra = R"E7(
export function echo(s: string, b: Uint8Array): string {
  let t = 0;
  for (let i = 0; i < b.length; i++) t += b[i];
  return s.length + ":" + t;
}
)E7";

int main(int argc, char** argv) {
  long calls = kWorkloadStress ? 3000 : 100000;
  int samples = 20;
  bool verbose = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--calls") && i + 1 < argc) calls = atol(argv[++i]);
    else if (!strcmp(argv[i], "--samples") && i + 1 < argc) samples = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-v")) verbose = true;
  }
  std::string src = std::string(kWorkloadSource) + kExtra;
  uint8_t* bc = nullptr;
  size_t bl = 0;
  char* errs = nullptr;
  if (e7_compile(src.data(), src.size(), "soak.ts", &bc, &bl, &errs) != E7_OK) {
    fprintf(stderr, "FAIL compile: %s\n", errs ? errs : "?");
    return 1;
  }
  e7_config cfg;
  e7_config_init(&cfg);
  cfg.memory_limit = 64u << 20;
  cfg.suspend_on_budget = 1;
  e7_vm* vm = e7_vm_new(&cfg);
  e7_register_native(vm, "hostMix", "ns>n", workloadHostMix, nullptr);
  e7_module* mod = nullptr;
  if (e7_load(vm, bc, bl, &mod) != E7_OK) {
    fprintf(stderr, "FAIL load: %s\n", e7_last_error(vm));
    return 1;
  }
  e7_free(bc);
  e7::VM* in = e7_testing_vm(vm);
  static const char* kFailing =
      "const xs: string[] = [];\n"
      "for (let i = 0; i < 200; i++) xs.push(\"x\" + i);\n"
      "if (xs.length > 0) throw new Error(\"init fails\");\n"
      "export function never(): number { return xs.length; }\n";
  if (e7_compile(kFailing, strlen(kFailing), "failing.ts", &bc, &bl, &errs) != E7_OK) {
    fprintf(stderr, "FAIL compile: %s\n", errs ? errs : "?");
    return 1;
  }
  std::vector<uint8_t> failing(bc, bc + bl);
  e7_free(bc);

  int failures = 0;
  long warmup = calls / 10;
  long every = (calls - warmup) / samples;
  if (every < 1) every = 1;
  std::vector<size_t> heap;
  std::vector<size_t> peaks;
  std::vector<size_t> process;
  uint8_t payload[64];
  for (int i = 0; i < 64; i++) payload[i] = (uint8_t)(i * 3);
  for (long i = 0; i < calls; i++) {
    e7_value ret;
    e7_status st;
    if (i % 2) {
      e7_value args[2];
      std::string s = "arg-" + std::to_string(i % 97);
      args[0].type = E7_STRING;
      args[0].v.str.ptr = s.c_str();
      args[0].v.str.len = s.size();
      args[1].type = E7_BYTES;
      args[1].v.bytes.ptr = payload;
      args[1].v.bytes.len = 1 + i % 64;
      e7_set_budget(vm, 0);
      st = e7_call(vm, mod, "echo", args, 2, &ret);
    } else {
      e7_value arg;
      arg.type = E7_NUMBER;
      arg.v.num = (double)(i % 211);
      // every 8th call runs in slices of 100 ticks (suspend / resume)
      bool sliced = i % 16 == 0;
      e7_set_budget(vm, sliced ? 100 : 0);
      st = e7_call(vm, mod, kWorkloadFunctions[(i / 2) % kWorkloadFunctionCount], &arg, 1, &ret);
    }
    while (st == E7_SUSPENDED) {
      e7_set_budget(vm, 100);
      st = e7_resume(vm, &ret);
    }
    if (st != E7_OK) {
      fprintf(stderr, "FAIL call %ld: status %d: %s\n", i, (int)st, e7_last_error(vm));
      return 1;
    }
    // now and then a module whose initializer fails: nothing of it may stay behind
    if (i % 1000 == 500) {
      e7_module* bad = nullptr;
      if (e7_load(vm, failing.data(), failing.size(), &bad) != E7_ERR_RUNTIME) {
        fprintf(stderr, "FAIL call %ld: the failing module did not fail\n", i);
        return 1;
      }
    }
    if (i >= warmup && (i - warmup) % every == 0) {
      e7_collect_garbage(vm);
      e7_gc_stats gs;
      memset(&gs, 0, sizeof gs);
      gs.struct_size = sizeof gs;
      e7_gc_stats_get(vm, &gs);
      heap.push_back(e7_memory_used(vm));
      process.push_back(processHeapInUse());
      peaks.push_back(gs.peak_heap_bytes);
      std::string why;
      if (!in->checkHeap(&why)) {
        fprintf(stderr, "FAIL call %ld: %s\n", i, why.c_str());
        failures++;
      }
      if (verbose)
        printf("call %7ld: heap after GC %zu, peak %zu, process heap %zu, collections %llu, max pause %.3f ms\n", i,
               heap.back(), gs.peak_heap_bytes, process.back(), (unsigned long long)gs.collections,
               gs.max_pause_ns / 1e6);
    }
  }
  size_t lo = heap[0], hi = heap[0];
  for (size_t h : heap) {
    lo = h < lo ? h : lo;
    hi = h > hi ? h : hi;
  }
  // tolerance: map / array capacities of the bounded global cache may still settle
  const size_t tol = 16 * 1024;
  if (hi - lo > tol) {
    fprintf(stderr, "FAIL heap after GC varies by %zu bytes (min %zu, max %zu)\n", hi - lo, lo, hi);
    failures++;
  }
  size_t n = heap.size(), firstHalf = 0, secondHalf = 0;
  for (size_t i = 0; i < n / 2; i++) firstHalf = heap[i] > firstHalf ? heap[i] : firstHalf;
  for (size_t i = n / 2; i < n; i++) secondHalf = heap[i] > secondHalf ? heap[i] : secondHalf;
  if (secondHalf > firstHalf + tol / 4) {
    fprintf(stderr, "FAIL heap grows: max %zu in the first half of the samples, %zu in the second\n", firstHalf,
            secondHalf);
    failures++;
  }
  // the peak heap may still rise when an unlucky collection threshold is hit late, but it must stay
  // within the GC growth factor of the live heap
  // the process heap (C++ side): no growth beyond allocator noise. The system allocator may still grow its
  // regions for the first ~150k calls (seen with macOS 27's malloc: about 350 KB in steps of 16-64 KB, then
  // flat for a million calls), so compare the halves like the VM heap above instead of the first sample: a
  // leak of a few bytes per call still shows up as growth between the halves.
  size_t procFirst = 0, procSecond = 0;
  for (size_t i = 0; i < n / 2; i++) procFirst = process[i] > procFirst ? process[i] : procFirst;
  for (size_t i = n / 2; i < n; i++) procSecond = process[i] > procSecond ? process[i] : procSecond;
  if (procFirst && procSecond > procFirst + (256u << 10)) {
    fprintf(stderr, "FAIL process heap grows: max %zu in the first half of the samples, %zu in the second\n",
            procFirst, procSecond);
    failures++;
  }
  if (peaks.back() > 4 * hi + (2u << 20)) {
    fprintf(stderr, "FAIL peak heap %zu is far above the live heap %zu\n", peaks.back(), hi);
    failures++;
  }
  printf("%ld calls, heap after GC %zu..%zu bytes over %zu samples, peak %zu, process heap %zu -> %zu, %d failures\n",
         calls, lo, hi, n, peaks.back(), process.front(), process.back(), failures);
  e7_vm_free(vm);
  return failures ? 1 : 0;
}
