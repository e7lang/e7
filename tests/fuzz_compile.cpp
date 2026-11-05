// Mutation fuzzer for the compiler: mutated E7 programs must either fail to compile with a diagnostic
// or produce bytecode that passes verification and runs (under budget / memory limits) without crashing.
// "internal compiler error" diagnostics are reported as failures.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <set>
#include <vector>

#include "e7/e7.h"

static std::vector<std::string> tokensOf(const std::string& s) {
  std::vector<std::string> t;
  std::string cur;
  for (char c : s) {
    bool word = isalnum((unsigned char)c) || c == '_' || c == '$';
    if (word) { cur += c; continue; }
    if (!cur.empty()) { t.push_back(cur); cur.clear(); }
    t.push_back(std::string(1, c));
  }
  if (!cur.empty()) t.push_back(cur);
  return t;
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: fuzz_compile <iterations> <file.ts>...\n"); return 2; }
  int iters = atoi(argv[1]);
  std::vector<std::string> corpus;
  for (int i = 2; i < argc; i++) {
    std::ifstream f(argv[i]);
    std::stringstream ss;
    ss << f.rdbuf();
    if (!ss.str().empty() && ss.str().find("import") == std::string::npos) corpus.push_back(ss.str());
  }
  const char* seedEnv = getenv("E7_FUZZ_SEED");
  std::mt19937 rng(seedEnv ? (unsigned)atoi(seedEnv) : 777u);
  std::set<std::string> seen;
  int compiled = 0, ran = 0, ice = 0;
  static const char* snippets[] = {"null", "undefined", "0", "-1", "1e309", "\"s\"", "[]", "{}", "(", ")", "{", "}", ";", ",",
                                   "?.", "!", "??", "=>", "u32(", "new Map<string, number>()", "this", "super", "x", "as number",
                                   "...", "`${", "0xffffffffffffffffff", "123n", ":", "?", "<", ">", "let", "const", "return"};
  for (int it = 0; it < iters; it++) {
    const std::string& base = corpus[rng() % corpus.size()];
    std::vector<std::string> t = tokensOf(base);
    int nm = 1 + (int)(rng() % 3);
    for (int k = 0; k < nm && !t.empty(); k++) {
      size_t p = rng() % t.size();
      switch (rng() % 5) {
        case 0: t.erase(t.begin() + p); break;
        case 1: t.insert(t.begin() + p, t[rng() % t.size()]); break;
        case 2: std::swap(t[p], t[rng() % t.size()]); break;
        case 3: t[p] = snippets[rng() % (sizeof snippets / sizeof *snippets)]; break;
        default: t.insert(t.begin() + p, snippets[rng() % (sizeof snippets / sizeof *snippets)]); break;
      }
    }
    std::string src;
    for (auto& s : t) src += s;
    uint8_t* out = nullptr;
    size_t len = 0;
    char* errs = nullptr;
    e7_compile_options co;  // every other program with debug information (it must pass the verifier too)
    memset(&co, 0, sizeof co);
    co.flags = (it % 2) ? E7_COMPILE_DEBUG : 0;
    e7_status st = e7_compile_ex(src.data(), src.size(), "fuzz.ts", &co, &out, &len, &errs);
    if (st != E7_OK) {
      if (errs && strstr(errs, "internal")) {
        ice++;
        std::string key(errs);
        key = key.substr(0, key.find("pc "));
        if (seen.insert(key).second) fprintf(stderr, "---- internal error #%d:\n%s\n---- source:\n%s\n", ice, errs, src.c_str());
      }
      e7_free(errs);
      continue;
    }
    compiled++;
    e7_config cfg;
    e7_config_init(&cfg);
    cfg.memory_limit = 32u << 20;
    cfg.max_frames = 2000;
    e7_vm* vm = e7_vm_new(&cfg);
    e7_set_print(vm, [](void*, const char*, size_t) {}, nullptr);
    e7_set_budget(vm, 2000000);
    e7_module* m;
    if (e7_load(vm, out, len, &m) == E7_OK) ran++;
    else if (e7_last_error(vm) && strstr(e7_last_error(vm), "verification")) {
      ice++;
      if (seen.insert(e7_last_error(vm)).second)
        fprintf(stderr, "---- verification failure: %s\n---- source:\n%s\n", e7_last_error(vm), src.c_str());
    }
    e7_vm_free(vm);
    e7_free(out);
  }
  printf("fuzz_compile: %d iterations, %d compiled, %d ran, %d internal errors\n", iters, compiled, ran, ice);
  return ice ? 1 : 0;
}
