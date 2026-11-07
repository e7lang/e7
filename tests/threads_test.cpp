// Several VMs on several threads at once, through the C API only and
// following its contract (each e7_vm is used by one thread; e7_debug_pause may be called from any
// thread). Every worker compiles the workload itself (the compiler runs concurrently too), creates
// its own VM (half of them with suspension and small budget slices), runs the same sequence of calls
// and must get exactly the results of a single-threaded reference run. One more VM runs a loop under
// a synchronous debugger while another thread keeps calling e7_debug_pause on it.
// Built with -fsanitize=thread (CMake option E7_TSAN) in CI, this finds shared mutable state in the
// VM, the standard library, the crypto code and the compiler.
//
// usage: threads_test [threads] [iterations]
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "e7/e7.h"
#include "workload.h"

static std::atomic<int> failures{0};

static void fail(const std::string& m) {
  fprintf(stderr, "FAIL %s\n", m.c_str());
  failures++;
}

static std::vector<uint8_t> compileWorkload(bool debug) {
  e7_compile_options co;
  e7_compile_options_init(&co);
  if (debug) co.flags = E7_COMPILE_DEBUG;
  uint8_t* out = nullptr;
  size_t len = 0;
  char* errs = nullptr;
  if (e7_compile_ex(kWorkloadSource, strlen(kWorkloadSource), "workload.ts", &co, &out, &len, &errs) != E7_OK) {
    fail(std::string("workload does not compile: ") + (errs ? errs : "?"));
    e7_free(errs);
    return {};
  }
  std::vector<uint8_t> v(out, out + len);
  e7_free(out);
  return v;
}

static std::string show(e7_status st, const e7_value& r, e7_vm* vm) {
  if (st != E7_OK) return "error " + std::to_string((int)st) + ": " + e7_last_error(vm);
  char b[64];
  switch (r.type) {
    case E7_STRING: return std::string(r.v.str.ptr, r.v.str.len);
    case E7_NUMBER: snprintf(b, sizeof b, "%.17g", r.v.num); return b;
    default: return "?";
  }
}

struct Printed {
  std::string text;
};
static void onPrint(void* user, const char* s, size_t n) { static_cast<Printed*>(user)->text.append(s, n); }

// The call sequence: function i % count with seed i * 7 % 101. Returns the results.
static std::vector<std::string> runSequence(const std::vector<uint8_t>& bc, int iterations, int64_t slice) {
  std::vector<std::string> results;
  e7_config cfg;
  e7_config_init(&cfg);
  cfg.memory_limit = 32u << 20;
  if (slice) cfg.suspend_on_budget = 1;
  e7_vm* vm = e7_vm_new(&cfg);
  if (!vm) { fail("e7_vm_new"); return results; }
  Printed printed;
  e7_set_print(vm, onPrint, &printed);
  e7_seed_random(vm, 1);
  e7_register_native(vm, "hostMix", "ns>n", workloadHostMix, nullptr);
  e7_module* mod = nullptr;
  if (e7_load(vm, bc.data(), bc.size(), &mod) != E7_OK) {
    fail(std::string("load: ") + e7_last_error(vm));
    e7_vm_free(vm);
    return results;
  }
  for (int i = 0; i < iterations; i++) {
    const char* fn = kWorkloadFunctions[i % kWorkloadFunctionCount];
    e7_value arg, ret;
    arg.type = E7_NUMBER;
    arg.v.num = (double)(i * 7 % 101);
    if (slice) e7_set_budget(vm, slice);
    e7_status st = e7_call(vm, mod, fn, &arg, 1, &ret);
    while (st == E7_SUSPENDED) {
      e7_set_budget(vm, slice);
      st = e7_resume(vm, &ret);
    }
    results.push_back(show(st, ret, vm));
    if (i % 50 == 49) {
      e7_collect_garbage(vm);
      e7_gc_stats gs;
      memset(&gs, 0, sizeof gs);
      gs.struct_size = sizeof gs;
      e7_gc_stats_get(vm, &gs);
      if (gs.heap_bytes != e7_memory_used(vm)) fail("gc stats disagree with e7_memory_used");
    }
  }
  e7_vm_free(vm);
  return results;
}

static std::atomic<int> debugStops{0};
static e7_debug_action onStop(e7_vm*, const e7_debug_stop* stop, void*) {
  if (stop->reason != E7_STOP_PAUSE && stop->reason != E7_STOP_ENTRY) fail("unexpected debugger stop reason");
  debugStops++;
  return E7_DEBUG_CONTINUE;
}

int main(int argc, char** argv) {
  int nthreads = argc > 1 ? atoi(argv[1]) : 8;
  int iterations = argc > 2 ? atoi(argv[2]) : (kWorkloadStress ? 30 : 300);
  std::vector<uint8_t> bc = compileWorkload(false);
  if (bc.empty()) return 1;
  std::vector<std::string> reference = runSequence(bc, iterations, 0);
  if ((int)reference.size() != iterations) return 1;
  for (auto& r : reference)
    if (r.rfind("error", 0) == 0) { fail("reference run: " + r); return 1; }

  std::vector<std::thread> workers;
  for (int t = 0; t < nthreads; t++) {
    workers.emplace_back([t, iterations, &reference] {
      std::vector<uint8_t> mine = compileWorkload(t % 3 == 2);  // the compiler runs on every thread
      if (mine.empty()) return;
      int64_t slice = t % 2 ? 50 + 37 * t : 0;
      std::vector<std::string> got = runSequence(mine, iterations, slice);
      if (got.size() != reference.size()) { fail("thread " + std::to_string(t) + ": incomplete run"); return; }
      for (size_t i = 0; i < got.size(); i++)
        if (got[i] != reference[i]) {
          fail("thread " + std::to_string(t) + " call " + std::to_string(i) + ": got '" + got[i] + "', expected '" +
               reference[i] + "'");
          return;
        }
    });
  }

  // a debugged VM, paused from another thread while it runs
  std::vector<uint8_t> dbc = compileWorkload(true);
  e7_vm* dvm = e7_vm_new(nullptr);
  e7_register_native(dvm, "hostMix", "ns>n", workloadHostMix, nullptr);
  e7_debug_attach(dvm, onStop, nullptr);
  e7_module* dmod = nullptr;
  if (e7_load(dvm, dbc.data(), dbc.size(), &dmod) != E7_OK) fail(std::string("debug VM load: ") + e7_last_error(dvm));
  std::atomic<bool> running{true};
  std::thread pauser([&] {
    while (running.load()) {
      e7_debug_pause(dvm);
      std::this_thread::yield();
    }
  });
  int spins = kWorkloadStress ? 20 : 200;
  for (int i = 0; i < spins && dmod; i++) {
    e7_value arg, ret;
    arg.type = E7_NUMBER;
    arg.v.num = 2000;
    e7_status st = e7_call(dvm, dmod, "spin", &arg, 1, &ret);
    if (st != E7_OK || ret.type != E7_NUMBER || ret.v.num != 689634) {
      fail("debugged VM: spin returned " + show(st, ret, dvm));
      break;
    }
  }
  running = false;
  pauser.join();
  e7_vm_free(dvm);

  for (auto& w : workers) w.join();
  printf("%d threads x %d calls, %d debugger stops, %d failures\n", nthreads, iterations, debugStops.load(),
         failures.load());
  return failures ? 1 : 0;
}
