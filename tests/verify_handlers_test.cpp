// try/catch handler tables come from bytecode, so the verifier must reject every malformed one:
// ranges and targets outside the code or inside an instruction, a non-reference error register,
// overlapping ranges, an inner handler listed after its enclosing one.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "compiler/compiler.h"
#include "core/module.h"
#include "e7/e7.h"

static int failures = 0;
#define CHECK(cond)                                                             \
  do {                                                                          \
    if (!(cond)) {                                                              \
      fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                               \
    }                                                                           \
  } while (0)

static const char* kSrc =
    "export function main(): number {\n"
    "  let n = 0;\n"
    "  try {\n"
    "    try { n += 1; throw new Error(\"x\"); } catch (e) { n += 10; }\n"
    "    throw new TypeError(\"y\");\n"
    "  } catch (e) { n += 100; }\n"
    "  return n;\n"
    "}\n";

static int run(const e7::Module& m, std::string* err = nullptr) {
  std::vector<uint8_t> b = e7::serializeModule(m);
  e7_vm* vm = e7_vm_new(nullptr);
  e7_module* mod = nullptr;
  int st = e7_load(vm, b.data(), b.size(), &mod);
  if (st == E7_OK) {
    e7_value r;
    st = e7_call(vm, mod, "main", nullptr, 0, &r);
    if (st == E7_OK && !(r.type == E7_NUMBER && r.v.num == 111)) st = -1;
  }
  if (err) *err = e7_last_error(vm);
  e7_vm_free(vm);
  return st;
}

int main() {
  e7::CompileOutput co = e7::compileSource("h.ts", kSrc);
  CHECK(co.ok);
  if (!co.ok) {
    fprintf(stderr, "%s", co.errorText().c_str());
    return 1;
  }
  size_t fi = co.module.exports[0].func;
  CHECK(co.module.funcs[fi].handlers.size() == 2);
  CHECK(run(co.module) == E7_OK);

  auto mutate = [&](const char* what, const std::function<void(e7::FuncDesc&)>& f) {
    e7::Module m = co.module;
    f(m.funcs[fi]);
    std::string err;
    int st = run(m, &err);
    if (st != E7_ERR_VERIFY) {
      fprintf(stderr, "%s: expected a verification error, got %d (%s)\n", what, st, err.c_str());
      failures++;
    }
  };
  size_t n = co.module.funcs[fi].code.size();
  mutate("empty range", [](e7::FuncDesc& d) { d.handlers[0].end = d.handlers[0].start; });
  mutate("range past the end", [&](e7::FuncDesc& d) { d.handlers[1].end = (uint32_t)n + 1; });
  mutate("handler past the end", [&](e7::FuncDesc& d) { d.handlers[0].handler = (uint32_t)n; });
  mutate("register out of range", [](e7::FuncDesc& d) { d.handlers[0].reg = (uint32_t)d.regKinds.size(); });
  mutate("number register", [](e7::FuncDesc& d) {
    for (uint32_t r = 0; r < d.regKinds.size(); r++)
      if (d.regKinds[r] != e7::K_R) { d.handlers[0].reg = r; return; }
  });
  mutate("outer before inner", [](e7::FuncDesc& d) { std::swap(d.handlers[0], d.handlers[1]); });
  mutate("overlap", [](e7::FuncDesc& d) {
    d.handlers[1].start = d.handlers[0].start + 1;  // starts inside the inner range...
    d.handlers[1].end = d.handlers[0].end + 1;      // ...and ends after it
  });
  mutate("too many handlers", [](e7::FuncDesc& d) { d.handlers.resize(70000, d.handlers[0]); });
  // targets inside multi-word instructions (the operand words of a call)
  for (size_t pc = 0; pc < n; pc++) {
    uint16_t op = e7::insOp(co.module.funcs[fi].code[pc]);
    if (op == e7::OP_CALLN || op == e7::OP_CALL) {
      mutate("handler inside an instruction", [&](e7::FuncDesc& d) { d.handlers[0].handler = (uint32_t)pc + 1; });
      mutate("range starting inside an instruction", [&](e7::FuncDesc& d) { d.handlers[1].start = (uint32_t)pc + 1; });
      break;
    }
  }
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("verify_handlers_test: all checks passed\n");
  return 0;
}
