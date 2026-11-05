// Mutation fuzzer for the bytecode loader / verifier / interpreter (deterministic, no libFuzzer needed).
// Takes valid modules, flips bits / bytes, fixes the checksum so the mutation reaches the verifier,
// then loads and runs. The VM must never crash: every outcome is either success or a clean error.
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "core/module.h"
#include "e7/e7.h"

namespace e7 { uint32_t crc32(const uint8_t* p, size_t n); }

static const char* kPrograms[] = {
    "export function main(): number { let s = 0; for (let i = 0; i < 100; i++) s += i * 2; return s; }",
    "class P { constructor(public x: number, public y: string) {} m(): string { return this.y + this.x; } }\n"
    "export function main(): string { const a = [new P(1, 'a'), new P(2, 'b')]; return a.map(p => p.m()).join(','); }",
    "export function main(): string { const m = new Map<string, number[]>(); m.set('a', [1]); m.get('a')!.push(2);"
    " return JSON.stringify([...m.keys()]) + (m.get('a') ?? []).length; }",
    "function f(n: number): () => number { let c = n; return () => ++c; }\n"
    "export function main(): number { const g = f(3); g(); return g(); }",
    "export function main(): string { const b = new Uint8Array(4); b[0] = 7; const t: u32 = 0xffffffff; return b.join('') + (t + 1); }",
};

// Debugger attached to mutated modules with debug information: step through everything and inspect
// every frame, scope and variable (the debug section is untrusted input too).
static int stops = 0;
static void inspectAll(e7_vm* vm, uint32_t ref, int depth) {
  const e7_debug_variable* v;
  size_t n = e7_debug_variables(vm, ref, 0, 50, &v);
  std::vector<uint32_t> refs;
  for (size_t i = 0; i < n; i++) refs.push_back(v[i].ref);
  for (uint32_t r : refs)
    if (r && depth < 3) inspectAll(vm, r, depth + 1);
}
static e7_debug_action onStop(e7_vm* vm, const e7_debug_stop*, void*) {
  const e7_debug_frame* f;
  size_t nf = e7_debug_stack(vm, &f);
  for (size_t i = 0; i < nf && i < 4; i++) {
    const e7_debug_scope* sc;
    size_t ns = e7_debug_scopes(vm, i, &sc);
    std::vector<uint32_t> refs;
    for (size_t k = 0; k < ns; k++) refs.push_back(sc[k].ref);
    for (uint32_t r : refs) inspectAll(vm, r, 0);
    const e7_debug_variable* res;
    e7_debug_evaluate(vm, i, "a[0].x", &res);
  }
  return ++stops % 64 ? E7_DEBUG_STEP_IN : E7_DEBUG_CONTINUE;
}

int main(int argc, char** argv) {
  int iters = argc > 1 ? atoi(argv[1]) : 2000;
  std::mt19937 rng(12345);
  int loaded = 0, ran = 0;
  for (int variant = 0; variant < 2 * (int)(sizeof kPrograms / sizeof *kPrograms); variant++) {
    const char* src = kPrograms[variant / 2];
    bool debug = variant % 2;  // odd variants: with debug information and a debugger attached
    uint8_t* out;
    size_t len;
    char* errs = nullptr;
    e7_compile_options co;
    e7_compile_options_init(&co);
    co.flags = debug ? E7_COMPILE_DEBUG : 0;
    if (e7_compile_ex(src, strlen(src), "f.ts", &co, &out, &len, &errs) != E7_OK) {
      fprintf(stderr, "compile failed: %s\n", errs);
      return 1;
    }
    std::vector<uint8_t> base(out, out + len);
    e7_free(out);
    for (int it = 0; it < (debug ? iters / 3 : iters); it++) {
      std::vector<uint8_t> m = base;
      int nmut = 1 + (int)(rng() % 4);
      for (int k = 0; k < nmut; k++) {
        size_t pos = 12 + rng() % (m.size() - 20);
        switch (rng() % 3) {
          case 0: m[pos] ^= (uint8_t)(1u << (rng() % 8)); break;
          case 1: m[pos] = (uint8_t)rng(); break;
          default: m[pos] = (uint8_t)(m[pos] + 1); break;
        }
      }
      // repair checksum so that mutations reach the parser and verifier
      uint32_t c = e7::crc32(m.data(), m.size() - 8);
      for (int i = 0; i < 4; i++) m[m.size() - 4 + i] = (uint8_t)(c >> (8 * i));
      e7_config cfg;
      e7_config_init(&cfg);
      cfg.memory_limit = 16u << 20;
      e7_vm* vm = e7_vm_new(&cfg);
      e7_set_budget(vm, 200000);
      if (debug) {
        e7_debug_attach(vm, onStop, nullptr);
        e7_debug_set_breakpoint(vm, "f.ts", 1);
        e7_debug_stop_on_entry(vm);
      }
      e7_module* mod = nullptr;
      if (e7_load(vm, m.data(), m.size(), &mod) == E7_OK) {
        loaded++;
        e7_value ret;
        if (e7_has_function(mod, "main") && e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_OK) ran++;
      }
      e7_vm_free(vm);
    }
  }
  printf("fuzz_load: %d mutated modules loaded, %d ran to completion (no crashes)\n", loaded, ran);
  return 0;
}
