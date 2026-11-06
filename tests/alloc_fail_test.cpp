// Allocation-failure injection: for every program given, the N-th heap
// allocation / external-memory charge / temporary-memory check of a fresh VM fails, for N = 1, 2, 3,
// ... until the program completes without reaching the N-th allocation. For every N:
//   * no crash (and no ASan / UBSan report in the sanitizer jobs),
//   * the load (and the call of an exported zero-argument `main`, if any) ends with the status of the
//     run without injection, or with E7_ERR_MEMORY once the injected failure has fired; the output
//     printed before the failure is a prefix of the normal output,
//   * the heap is consistent right after the failure and after a full collection (VM::checkHeap:
//     byte count = sum of object sizes and their buffers, no dangling references, execution state
//     reset),
//   * the VM stays usable: a further module loads and its function returns the right value.
//
// usage: alloc_fail_test [--persistent] [--max-n N] [--stride-after N] [--exclude substring]... file.ts...
//   --persistent      every allocation from the N-th on fails, not only the N-th (error paths that
//                     allocate themselves)
//   --max-n N         stop after N injection points per program (0 = all; default 0)
//   --stride-after N  above N, test only every k-th point, k growing with N (default 4000), so that
//                     programs with very many allocations stay quick
//   --exclude S       skip the files whose path contains S
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "capi/testing.h"
#include "e7/e7.h"
#include "core/module.h"
#include "vm/vm.h"

static int failures = 0;
static bool gPersistent = false;  // --persistent: every allocation from the N-th on fails
static bool gDebugger = false;    // --debugger: run under a stepping debugger (see onStop)
static bool gSlice = false;       // --slice: budget slice of 1 (see runSliced)

static void collect(void* user, const char* s, size_t n) {
  auto* out = static_cast<std::string*>(user);
  out->append(s, n);
  out->push_back('\n');
}

static const char* kProbe =
    "export function probe(n: number): string {\n"
    "  const m = new Map<string, number[]>();\n"
    "  let s = \"\";\n"
    "  for (let i = 0; i < n; i++) { m.set(\"k\" + i, [i, i * 2]); s += String(i % 10); }\n"
    "  let t = 0;\n"
    "  for (const [k, v] of m) t += v[1] + k.length;\n"
    "  return s.length + \":\" + t;\n"
    "}\n";

struct RunResult {
  e7_status st = E7_OK;
  std::string out, err;
  int64_t loadFailedHeapDelta = 0;  // e7_load failed: heap after a collection minus the heap before
};

// --debugger: a synchronous debugger steps through everything; at every stop for an error (the
// injected memory error among them) and at every 32nd other stop it inspects the stack, the scopes and
// their variables
static uint64_t gStops = 0;
static e7_debug_action onStop(e7_vm* vm, const e7_debug_stop* stop, void*) {
  if (stop->reason != E7_STOP_EXCEPTION && ++gStops % 32 != 0) return E7_DEBUG_STEP_IN;
  const e7_debug_frame* frames = nullptr;
  size_t nf = e7_debug_stack(vm, &frames);
  for (size_t f = 0; f < nf && f < 8; f++) {
    const e7_debug_scope* sc = nullptr;
    size_t ns = e7_debug_scopes(vm, f, &sc);
    std::vector<uint32_t> refs;
    for (size_t k = 0; k < ns; k++) refs.push_back(sc[k].ref);
    for (uint32_t ref : refs) {
      const e7_debug_variable* vars = nullptr;
      size_t nv = e7_debug_variables(vm, ref, 0, 16, &vars);
      std::vector<uint32_t> kids;
      for (size_t i = 0; i < nv; i++)
        if (vars[i].ref) kids.push_back(vars[i].ref);
      for (uint32_t c : kids) e7_debug_variables(vm, c, 0, 16, &vars);
    }
  }
  return E7_DEBUG_STEP_IN;
}

// --slice: suspension at every budget check (VM internals: e7_load runs initializers unsliced)
static e7_status runSliced(e7_vm* vm, const std::vector<uint8_t>& bc, std::string& err) {
  e7::VM& in = *e7_testing_vm(vm);
  in.setSuspendOnBudget(true);
  in.setCallbackBudgetRefill(1);
  auto toEnd = [&](int st) {
    while (st == e7::ERR_SUSPENDED) {
      in.setBudget(1);
      st = in.resume();
    }
    return st;
  };
  try {
    e7::Module m;
    if (!e7::deserializeModule(bc.data(), bc.size(), m, err)) return E7_ERR_LOAD;
    e7::ModuleRT* rt = in.loadModule(std::move(m));
    in.setBudget(1);
    int st = toEnd(in.runInit(rt));
    if (st != e7::ERR_OK) return (e7_status)st;
    rt->initialized = true;
    for (auto& ex : rt->m.exports)
      if (ex.name == "main") {
        in.setBudget(1);
        st = toEnd(in.callEntry(&rt->funcs[ex.func], nullptr, 0));
        if (st != e7::ERR_OK) return (e7_status)st;
      }
    in.setSuspendOnBudget(false);
    in.setBudget(0);
    return E7_OK;
  } catch (e7::E7Error& e) {
    in.setSuspendOnBudget(false);
    in.setBudget(0);
    err = e.msg;
    return (e7_status)e.code;
  }
}

// Loads the module (and calls main() when it exports one) with the n-th allocation failing (n = 0: none).
static RunResult runOnce(const std::vector<uint8_t>& bc, uint64_t n, e7_vm** vmOut, uint64_t* allocs) {
  RunResult r;
  e7_vm* vm = e7_vm_new(nullptr);
  if (!vm) { fprintf(stderr, "cannot create VM\n"); exit(1); }
  e7_set_print(vm, collect, &r.out);
  e7_seed_random(vm, 42);
  if (gDebugger) {
    e7_debug_attach(vm, onStop, nullptr);
    e7_debug_break_on_caught_errors(vm, 1);
    e7_debug_stop_on_entry(vm);
  }
  e7::VM* in = e7_testing_vm(vm);
  in->injectAllocationFailure(n, gPersistent);
  if (gSlice) {
    r.st = runSliced(vm, bc, r.err);
    if (allocs) *allocs = in->allocationCount();
    *vmOut = vm;
    return r;
  }
  e7_module* mod = nullptr;
  size_t before = e7_memory_used(vm);
  r.st = e7_load(vm, bc.data(), bc.size(), &mod);
  if (r.st != E7_OK) {
    // a module whose loading failed is not kept: after a collection the heap is back at its baseline
    e7_collect_garbage(vm);
    r.loadFailedHeapDelta = (int64_t)e7_memory_used(vm) - (int64_t)before;
  }
  if (r.st == E7_OK && e7_has_function(mod, "main")) {
    e7_value ret;
    r.st = e7_call(vm, mod, "main", nullptr, 0, &ret);
  }
  if (r.st != E7_OK) r.err = e7_last_error(vm);
  if (allocs) *allocs = in->allocationCount();
  *vmOut = vm;
  return r;
}

static bool readFile(const char* path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

static void checkHeap(e7_vm* vm, const char* file, uint64_t n, const char* when) {
  std::string why;
  if (!e7_testing_vm(vm)->checkHeap(&why)) {
    fprintf(stderr, "FAIL %s, allocation %llu failing: heap inconsistent %s: %s\n", file, (unsigned long long)n, when,
            why.c_str());
    failures++;
  }
}

// After a failure: the VM must still load and run another module.
static void checkUsable(e7_vm* vm, const std::vector<uint8_t>& probe, const char* file, uint64_t n) {
  e7_testing_vm(vm)->injectAllocationFailure(0);
  if (gDebugger && e7_debug_detach(vm) != E7_OK) {
    fprintf(stderr, "FAIL %s, allocation %llu failing: cannot detach the debugger: %s\n", file, (unsigned long long)n,
            e7_last_error(vm));
    failures++;
  }
  e7_module* pm = nullptr;
  e7_status st = e7_load(vm, probe.data(), probe.size(), &pm);
  e7_value ret;
  e7_value arg;
  arg.type = E7_NUMBER;
  arg.v.num = 50;
  if (st == E7_OK) st = e7_call(vm, pm, "probe", &arg, 1, &ret);
  if (st != E7_OK || ret.type != E7_STRING || std::string(ret.v.str.ptr, ret.v.str.len) != "50:2590") {
    fprintf(stderr, "FAIL %s, allocation %llu failing: the VM is not usable afterwards (status %d: %s)\n", file,
            (unsigned long long)n, (int)st, st == E7_OK ? "wrong result" : e7_last_error(vm));
    failures++;
  }
}

int main(int argc, char** argv) {
  uint64_t maxN = 0, strideAfter = 4000;
  std::vector<const char*> files;
  std::vector<std::string> excludes;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--max-n") && i + 1 < argc) maxN = strtoull(argv[++i], nullptr, 10);
    else if (!strcmp(argv[i], "--stride-after") && i + 1 < argc) strideAfter = strtoull(argv[++i], nullptr, 10);
    else if (!strcmp(argv[i], "--exclude") && i + 1 < argc) excludes.push_back(argv[++i]);
    else if (!strcmp(argv[i], "--persistent")) gPersistent = true;
    else if (!strcmp(argv[i], "--debugger")) gDebugger = true;
    else if (!strcmp(argv[i], "--slice")) gSlice = true;
    else files.push_back(argv[i]);
  }
  for (size_t k = files.size(); k-- > 0;)
    for (auto& x : excludes)
      if (strstr(files[k], x.c_str())) {
        files.erase(files.begin() + (std::ptrdiff_t)k);
        break;
      }
  if (files.empty()) {
    fprintf(stderr, "usage: alloc_fail_test [--max-n N] [--stride-after N] [--exclude substring]... file.ts...\n");
    return 2;
  }
  uint8_t* pb = nullptr;
  size_t pl = 0;
  char* perr = nullptr;
  if (e7_compile(kProbe, strlen(kProbe), "probe.ts", &pb, &pl, &perr) != E7_OK) {
    fprintf(stderr, "probe does not compile: %s\n", perr ? perr : "?");
    return 1;
  }
  std::vector<uint8_t> probe(pb, pb + pl);
  e7_free(pb);

  uint64_t totalRuns = 0;
  int programs = 0;
  for (const char* file : files) {
    std::string src;
    if (!readFile(file, src)) { fprintf(stderr, "FAIL cannot read %s\n", file); failures++; continue; }
    if (src.find("// @error:") != std::string::npos) continue;  // compile-error tests: nothing to run
    uint8_t* bcp = nullptr;
    size_t bcl = 0;
    char* errs = nullptr;
    // imports resolve relative to the file (examples/, tests/cases/modules/): compile like `e7c` does
    e7_compile_options co;
    e7_compile_options_init(&co);
    if (gDebugger) co.flags = E7_COMPILE_DEBUG;
    co.read_file = [](void*, const char* path, char** data, size_t* len) -> int {
      std::string s;
      if (!readFile(path, s)) return 1;
      *data = (char*)malloc(s.size() + 1);
      memcpy(*data, s.data(), s.size());
      *len = s.size();
      return 0;
    };
    e7_status cst = e7_compile_ex(src.data(), src.size(), file, &co, &bcp, &bcl, &errs);
    if (cst != E7_OK) {
      fprintf(stderr, "skip %s (does not compile: %s)\n", file, errs ? errs : "?");
      e7_free(errs);
      continue;
    }
    std::vector<uint8_t> bc(bcp, bcp + bcl);
    e7_free(bcp);
    programs++;

    e7_vm* vm = nullptr;
    uint64_t total = 0;
    RunResult base = runOnce(bc, 0, &vm, &total);
    checkHeap(vm, file, 0, "after the normal run");
    e7_vm_free(vm);
    uint64_t points = 0;
    uint64_t step = 1;
    for (uint64_t n = 1;; n += step) {
      if (maxN && points >= maxN) break;
      if (n > strideAfter) step = 1 + n / strideAfter;  // thinning out: still covers every phase
      RunResult r = runOnce(bc, n, &vm, nullptr);
      e7::VM* in = e7_testing_vm(vm);
      bool fired = in->injectedFailureFired();
      points++;
      totalRuns++;
      if (!fired) {
        // the program completed before its n-th allocation: same result as without injection
        if (r.st != base.st || r.out != base.out) {
          fprintf(stderr, "FAIL %s: allocation %llu never reached, but the result differs (status %d, normal %d)\n",
                  file, (unsigned long long)n, (int)r.st, (int)base.st);
          failures++;
        }
        e7_vm_free(vm);
        break;
      }
      if (r.st != E7_ERR_MEMORY) {
        fprintf(stderr, "FAIL %s: allocation %llu failed, but the status is %d (expected E7_ERR_MEMORY %d): %s\n", file,
                (unsigned long long)n, (int)r.st, (int)E7_ERR_MEMORY, r.err.c_str());
        failures++;
      } else if (r.err.find("out of memory") == std::string::npos) {
        fprintf(stderr, "FAIL %s: allocation %llu: unexpected message: %s\n", file, (unsigned long long)n, r.err.c_str());
        failures++;
      }
      if (base.out.compare(0, r.out.size(), r.out) != 0) {
        fprintf(stderr, "FAIL %s: allocation %llu: output before the failure differs from the normal run\n", file,
                (unsigned long long)n);
        failures++;
      }
      checkHeap(vm, file, n, "right after the failure");
      e7_collect_garbage(vm);
      checkHeap(vm, file, n, "after a collection");
      if (r.loadFailedHeapDelta != 0) {
        fprintf(stderr, "FAIL %s, allocation %llu failing: e7_load failed, but %lld bytes stay allocated after a collection\n",
                file, (unsigned long long)n, (long long)r.loadFailedHeapDelta);
        failures++;
      }
      checkUsable(vm, probe, file, n);
      e7_collect_garbage(vm);
      checkHeap(vm, file, n, "after the probe");
      e7_vm_free(vm);
      if (failures > 20) break;
    }
    printf("%-60s %6llu allocations, %6llu injection points\n", file, (unsigned long long)total,
           (unsigned long long)points);
    fflush(stdout);
    if (failures > 20) break;
  }
  printf("%d programs, %llu runs, %d failures\n", programs, (unsigned long long)totalRuns, failures);
  return failures ? 1 : 0;
}
