// Equivalence runs (docs/ROADMAP.md 0.2.5, P0.3): every program runs once normally and once in each
// requested mode; status, printed output and error message (with its stack trace) must be identical.
//   slice  budget slice of 1 with suspension enabled: the VM suspends at every budget check (back edge,
//          call, catch) and is resumed, in the module initializer and in an exported main(). Inside
//          callbacks from natives (which cannot suspend) the budget is refilled instead.
//   step   a synchronous debugger single-steps through everything (E7_DEBUG_STEP_IN at every stop,
//          from the first statement, also stopping on caught errors) and at every stop walks the
//          stack, every scope of the innermost 64 frames and the variables in them (two levels deep),
//          and evaluates each variable by name. The stack, scopes and variables are inspected at the
//          first --inspect-limit stops of each program (default 5000), the stop information at every
//          stop, so that long loops and deep recursion stay quick.
// Modules are compiled with debug information, as `e7 file.ts` does. Files with `// @error:`
// (compile errors) are skipped.
//
// usage: equiv_test [--modes slice,step] [--inspect-limit N] [-v] file.ts...
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "capi/testing.h"
#include "core/module.h"
#include "e7/e7.h"
#include "vm/vm.h"

static bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

static void collect(void* user, const char* s, size_t n) {
  auto* out = static_cast<std::string*>(user);
  out->append(s, n);
  out->push_back('\n');
}

struct Result {
  int st = 0;
  std::string out, err;
  uint64_t stops = 0, suspensions = 0, inspected = 0;
};

static e7_vm* newVM(Result& r) {
  e7_vm* vm = e7_vm_new(nullptr);
  if (!vm) { fprintf(stderr, "cannot create VM\n"); exit(1); }
  e7_set_print(vm, collect, &r.out);
  e7_seed_random(vm, 7);
  return vm;
}

// ---------------------------------------------------------------- normal and step (C API)
static void inspectRef(e7_vm* vm, uint32_t ref, size_t frame, int depth, bool evaluate, Result& r) {
  const e7_debug_variable* vars = nullptr;
  size_t n = e7_debug_variables(vm, ref, 0, 24, &vars);
  struct V { std::string name; uint32_t ref; };
  std::vector<V> copy;
  for (size_t i = 0; i < n; i++) {
    // touch every string (ASan: they must be valid)
    r.inspected += strlen(vars[i].name) + strlen(vars[i].type) + strlen(vars[i].value) > 0 ? 1 : 0;
    copy.push_back(V{vars[i].name, vars[i].ref});
  }
  for (auto& v : copy) {
    if (evaluate) {
      // the variable, and the member accesses the evaluator knows (most fail for most types)
      for (const char* suffix : {"", ".length", ".size", "[0]", "[1].length", "['k']"}) {
        const e7_debug_variable* ev = nullptr;
        std::string expr = v.name + suffix;
        if (e7_debug_evaluate(vm, frame, expr.c_str(), &ev) == E7_OK && ev) r.inspected += strlen(ev->value) > 0;
      }
    }
    if (v.ref && depth < 2) inspectRef(vm, v.ref, frame, depth + 1, false, r);
  }
}

static uint64_t gInspectLimit = 5000;

static e7_debug_action onStop(e7_vm* vm, const e7_debug_stop* stop, void* user) {
  auto& r = *static_cast<Result*>(user);
  r.stops++;
  r.inspected += strlen(stop->message) + strlen(stop->where.function) > 0;
  e7_debug_stop info;
  if (e7_debug_stop_info(vm, &info) != E7_OK) {
    fprintf(stderr, "e7_debug_stop_info failed inside the handler\n");
    exit(1);
  }
  if (r.stops > gInspectLimit) return E7_DEBUG_STEP_IN;
  const e7_debug_frame* frames = nullptr;
  size_t nf = e7_debug_stack(vm, &frames);
  for (size_t f = 0; f < nf; f++) r.inspected += strlen(frames[f].function) + strlen(frames[f].file) > 0;
  // scopes and variables of the innermost 64 frames (deep recursion would make this quadratic)
  for (size_t f = 0; f < nf && f < 64; f++) {
    const e7_debug_scope* sc = nullptr;
    size_t ns = e7_debug_scopes(vm, f, &sc);
    std::vector<uint32_t> refs;
    for (size_t k = 0; k < ns; k++) refs.push_back(sc[k].ref);
    for (uint32_t ref : refs) inspectRef(vm, ref, f, 0, f == 0, r);
  }
  return E7_DEBUG_STEP_IN;
}

static Result runCapi(const std::vector<uint8_t>& bc, bool step) {
  Result r;
  e7_vm* vm = newVM(r);
  if (step) {
    e7_debug_attach(vm, onStop, &r);
    e7_debug_break_on_caught_errors(vm, 1);
    e7_debug_stop_on_entry(vm);
  }
  e7_module* mod = nullptr;
  e7_status st = e7_load(vm, bc.data(), bc.size(), &mod);
  if (st == E7_OK && e7_has_function(mod, "main")) {
    e7_value ret;
    st = e7_call(vm, mod, "main", nullptr, 0, &ret);
  }
  r.st = st;
  if (st != E7_OK) r.err = e7_last_error(vm);
  std::string why;
  if (!e7_testing_vm(vm)->checkHeap(&why)) {
    r.st = -1;
    r.err += "\n[heap check failed: " + why + "]";
  }
  e7_vm_free(vm);
  return r;
}

// ---------------------------------------------------------------- slice (VM internals: e7_load runs
// module initializers without suspension)
static Result runSliced(const std::vector<uint8_t>& bc) {
  Result r;
  e7_vm* vm = newVM(r);
  e7::VM& in = *e7_testing_vm(vm);
  in.setSuspendOnBudget(true);
  in.setCallbackBudgetRefill(1);
  auto runToEnd = [&](int st) {
    while (st == e7::ERR_SUSPENDED) {
      r.suspensions++;
      in.setBudget(1);
      st = in.resume();
    }
    return st;
  };
  try {
    e7::Module m;
    std::string err;
    if (!e7::deserializeModule(bc.data(), bc.size(), m, err)) throw e7::E7Error(e7::ERR_LOAD, err);
    e7::ModuleRT* rt = in.loadModule(std::move(m));
    in.setBudget(1);
    int st = runToEnd(in.runInit(rt));
    if (st != e7::ERR_OK) throw e7::E7Error(st, "unexpected status");
    rt->initialized = true;
    for (auto& ex : rt->m.exports) {
      if (ex.name != "main") continue;
      in.setBudget(1);
      st = runToEnd(in.callEntry(&rt->funcs[ex.func], nullptr, 0));
      if (st != e7::ERR_OK) throw e7::E7Error(st, "unexpected status");
    }
    r.st = E7_OK;
  } catch (e7::E7Error& e) {
    r.st = e.code;
    r.err = e.msg;
  } catch (std::bad_alloc&) {
    in.resetExecution();
    r.st = E7_ERR_MEMORY;
    r.err = "out of memory";
  }
  std::string why;
  if (!in.checkHeap(&why)) {
    r.st = -1;
    r.err += "\n[heap check failed: " + why + "]";
  }
  e7_vm_free(vm);
  return r;
}

static int readFileCb(void*, const char* path, char** data, size_t* len) {
  std::string s;
  if (!readFile(path, s)) return 1;
  *data = (char*)malloc(s.size() + 1);
  memcpy(*data, s.data(), s.size());
  *len = s.size();
  return 0;
}

static std::string firstDiff(const std::string& a, const std::string& b) {
  size_t i = 0;
  while (i < a.size() && i < b.size() && a[i] == b[i]) i++;
  size_t from = i > 80 ? i - 80 : 0;
  return "normal: ..." + a.substr(from, 200) + "\n  other:  ..." + b.substr(from, 200);
}

int main(int argc, char** argv) {
  bool slice = true, step = true, verbose = false;
  std::vector<std::string> files;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--modes" && i + 1 < argc) {
      std::string m = argv[++i];
      slice = m.find("slice") != std::string::npos;
      step = m.find("step") != std::string::npos;
    } else if (a == "--inspect-limit" && i + 1 < argc) {
      gInspectLimit = strtoull(argv[++i], nullptr, 10);
    } else if (a == "-v") {
      verbose = true;
    } else {
      files.push_back(a);
    }
  }
  if (files.empty()) {
    fprintf(stderr, "usage: equiv_test [--modes slice,step] [-v] file.ts...\n");
    return 2;
  }
  int failures = 0, programs = 0;
  uint64_t stops = 0, suspensions = 0;
  for (auto& file : files) {
    std::string src;
    if (!readFile(file, src)) { fprintf(stderr, "FAIL cannot read %s\n", file.c_str()); failures++; continue; }
    if (src.find("// @error:") != std::string::npos) continue;
    e7_compile_options co;
    e7_compile_options_init(&co);
    co.flags = E7_COMPILE_DEBUG;
    co.read_file = readFileCb;
    uint8_t* bcp = nullptr;
    size_t bcl = 0;
    char* errs = nullptr;
    if (e7_compile_ex(src.data(), src.size(), file.c_str(), &co, &bcp, &bcl, &errs) != E7_OK) {
      fprintf(stderr, "FAIL %s does not compile: %s\n", file.c_str(), errs ? errs : "?");
      e7_free(errs);
      failures++;
      continue;
    }
    std::vector<uint8_t> bc(bcp, bcp + bcl);
    e7_free(bcp);
    programs++;
    Result base = runCapi(bc, false);
    auto compare = [&](const char* mode, const Result& r) {
      if (r.st == base.st && r.out == base.out && r.err == base.err) return;
      failures++;
      fprintf(stderr, "FAIL %s (%s): status %d, normal %d\n", file.c_str(), mode, r.st, base.st);
      if (r.out != base.out) fprintf(stderr, "  output differs:\n  %s\n", firstDiff(base.out, r.out).c_str());
      if (r.err != base.err) fprintf(stderr, "  error differs:\n  %s\n", firstDiff(base.err, r.err).c_str());
    };
    if (base.st == -1) {
      failures++;
      fprintf(stderr, "FAIL %s (normal): %s\n", file.c_str(), base.err.c_str());
    }
    std::string line = file;
    if (slice) {
      Result r = runSliced(bc);
      compare("slice 1", r);
      suspensions += r.suspensions;
      line += " suspensions=" + std::to_string(r.suspensions);
    }
    if (step) {
      Result r = runCapi(bc, true);
      compare("debugger stepping", r);
      stops += r.stops;
      line += " stops=" + std::to_string(r.stops);
    }
    if (verbose) printf("%s\n", line.c_str());
  }
  printf("%d programs, %llu suspensions, %llu debugger stops, %d failures\n", programs,
         (unsigned long long)suspensions, (unsigned long long)stops, failures);
  return failures ? 1 : 0;
}
