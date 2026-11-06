// Coroutines and records with a presence mask: the verifier must reject every malformed use of the
// instructions and flags (GEN_START / ASYNC_START / YIELD / RESUME_* / AWAIT / FIELD_ABSENT, fflags,
// cflags), and the run-time checks must stop wrong dynamic uses without memory unsafety.
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
    "interface Opt { a: number; b?: string | undefined }\n"
    "function* gen(n: number): Generator<number, number, number> {\n"
    "  let t = 0;\n"
    "  for (let i = 0; i < n; i++) t += yield i;\n"
    "  return t;\n"
    "}\n"
    "async function twice(x: number): Promise<number> { return (await Promise.resolve(x)) * 2; }\n"
    "export async function main(): Promise<number> {\n"
    "  const g = gen(3);\n"
    "  let s = 0;\n"
    "  let r = g.next(0);\n"
    "  while (!r.done) { s += r.value; r = g.next(1); }\n"
    "  const o: Opt = { a: 1 };\n"
    "  return s + r.value * 10 + (await twice(5)) + Object.keys(o).length * 100;\n"
    "}\n";

static int run(const e7::Module& m, std::string* err = nullptr, double* out = nullptr) {
  std::vector<uint8_t> b = e7::serializeModule(m);
  e7_vm* vm = e7_vm_new(nullptr);
  e7_module* mod = nullptr;
  int st = e7_load(vm, b.data(), b.size(), &mod);
  if (st == E7_OK) {
    e7_value r;
    st = e7_call(vm, mod, "main", nullptr, 0, &r);
    if (st == E7_OK && out) *out = r.v.num;
  }
  if (err) *err = e7_last_error(vm);
  e7_vm_free(vm);
  return st;
}

static size_t funcNamed(const e7::Module& m, const std::string& n) {
  for (size_t i = 0; i < m.funcs.size(); i++)
    if (m.funcs[i].name == n) return i;
  return SIZE_MAX;
}

static size_t findOp(const e7::FuncDesc& f, uint16_t op) {
  for (size_t pc = 0; pc < f.code.size(); pc += e7::insWords(f.code[pc]))
    if (e7::insOp(f.code[pc]) == op) return pc;
  return SIZE_MAX;
}

int main() {
  e7::CompileOutput co = e7::compileSource("c.ts", kSrc);
  CHECK(co.ok);
  if (!co.ok) {
    fprintf(stderr, "%s", co.errorText().c_str());
    return 1;
  }
  double v = 0;
  std::string err;
  CHECK(run(co.module, &err, &v) == E7_OK);
  // yields 0+1+2, returns t = 1+1+... (the three values sent after the first next), 10 from twice, 1 key
  CHECK(v == 3 + 3 * 10 + 10 + 100);  // yields 0+1+2, returns the three values sent (1+1+1), twice(5), one key
  size_t gi = funcNamed(co.module, "gen"), ai = funcNamed(co.module, "twice"), mi = funcNamed(co.module, "main");
  CHECK(gi != SIZE_MAX && ai != SIZE_MAX && mi != SIZE_MAX);
  if (failures) return 1;

  auto expectReject = [&](const char* what, const std::function<void(e7::Module&)>& f, const char* msg) {
    e7::Module m = co.module;
    f(m);
    std::string e;
    int st = run(m, &e);
    if (st == E7_OK || e.find(msg) == std::string::npos) {
      fprintf(stderr, "FAIL %s: status %d, error '%s' (expected '%s')\n", what, st, e.c_str(), msg);
      failures++;
    }
  };

  expectReject("generator without GEN_START", [&](e7::Module& m) { m.funcs[gi].code[0] = e7::encode(e7::OP_NOP); },
               "coroutine without its start instruction");
  expectReject("GEN_START in a plain function", [&](e7::Module& m) { m.funcs[gi].fflags = 0; }, "misplaced coroutine start");
  expectReject("ASYNC_START in a plain function", [&](e7::Module& m) { m.funcs[ai].fflags = 0; }, "misplaced coroutine start");
  expectReject("generator flags on an async function",
               [&](e7::Module& m) { m.funcs[ai].fflags = e7::makeFflags(e7::FK_GENERATOR, 2, 0, 0); },
               "coroutine without its start instruction");
  expectReject("yielded kind does not match the register",
               [&](e7::Module& m) { auto& f = m.funcs[gi]; f.fflags = e7::makeFflags(e7::FK_GENERATOR, 3, e7::fflagsNext(f.fflags), 0); },
               "register");
  expectReject("return kind other than the yielded kind",
               [&](e7::Module& m) { auto& f = m.funcs[gi]; f.fflags = e7::makeFflags(e7::FK_GENERATOR, 2, 2, 3); },
               "bad generator flags");
  expectReject("plain function with value-kind flags", [&](e7::Module& m) { m.funcs[mi].fflags = (uint8_t)(m.funcs[mi].fflags | 0x04); },
               "bad function flags");
  expectReject("ASYNC_START with another value type",
               [&](e7::Module& m) {
                 auto& c = m.funcs[ai].code[0];
                 c = e7::encode(e7::OP_ASYNC_START, 10 /* VT_REF */);
               },
               "promise value type does not match");
  expectReject("YIELD metadata with high bits", [&](e7::Module& m) {
    auto& f = m.funcs[gi];
    size_t pc = findOp(f, e7::OP_YIELD);
    f.code[pc + 1] |= 1ull << 40;
  }, "bad yield metadata");
  expectReject("YIELD return register of the wrong kind", [&](e7::Module& m) {
    auto& f = m.funcs[gi];
    size_t pc = findOp(f, e7::OP_YIELD);
    uint16_t intReg = 0;
    for (size_t r = 0; r < f.regKinds.size(); r++)
      if (f.regKinds[r] == e7::K_I) { intReg = (uint16_t)r; break; }
    f.code[pc + 1] = intReg;
  }, "expected F");
  expectReject("RESUME mode 3", [&](e7::Module& m) {
    auto& f = m.funcs[mi];
    size_t pc = findOp(f, e7::OP_RESUME_F);
    f.code[pc + 1] |= 3ull << 16;
  }, "bad resume metadata");
  expectReject("RESUME without a value but with a register", [&](e7::Module& m) {
    auto& f = m.funcs[mi];
    size_t pc = findOp(f, e7::OP_RESUME_F);
    f.code[pc + 1] = (f.code[pc + 1] & ~(3ull << 18)) | 0;  // value kind none, the register kept
  }, "bad resume value register");
  expectReject("RESUME throw() with a number", [&](e7::Module& m) {
    auto& f = m.funcs[mi];
    size_t pc = findOp(f, e7::OP_RESUME_F);
    f.code[pc + 1] = (f.code[pc + 1] & ~(3ull << 16)) | (1ull << 16);  // throw mode, value kind F kept
  }, "throw() needs an error object");
  expectReject("AWAIT in a plain function", [&](e7::Module& m) {
    auto& f = m.funcs[ai];
    f.fflags = 0;
    f.code[0] = e7::encode(e7::OP_NOP);
  }, "await outside an async function");
  expectReject("presence flag on a class that is not a record", [&](e7::Module& m) {
    for (auto& c : m.classes)
      if (c.isRecord == 1) { c.isRecord = 0; c.cflags = e7::kClassFlagPresence; break; }
  }, "bad class flags");
  expectReject("unknown class flag", [&](e7::Module& m) {
    for (auto& c : m.classes)
      if (c.isRecord == 1) { c.cflags = 8; break; }
  }, "bad class flags");
  // run time: FIELD_ABSENT on a record whose class has no presence mask
  expectReject("FIELD_ABSENT without a presence mask", [&](e7::Module& m) {
    for (auto& c : m.classes) c.cflags = 0;
  }, "invalid field access");
  // run time: a generator whose kinds differ from the RESUME (the coroutine is an untyped reference)
  expectReject("RESUME of a coroutine with other kinds", [&](e7::Module& m) {
    auto& f = m.funcs[gi];
    f.fflags = e7::makeFflags(e7::FK_GENERATOR, 2, 3 /* next kind R */, 2);
    size_t pc = findOp(f, e7::OP_YIELD);
    // the received register must be an R register now
    uint16_t refReg = 0;
    for (size_t r = 0; r < f.regKinds.size(); r++)
      if (f.regKinds[r] == e7::K_R) { refReg = (uint16_t)r; break; }
    f.code[pc] = (f.code[pc] & ~(0xFFFFull << 32)) | ((uint64_t)refReg << 32);
  }, "generator type mismatch");

  // ---- async generators: function kind 3, requests to them (RESUME_R with kResumeAsync)
  {
    static const char* kAsyncSrc =
        "function* syncGen(): Generator<number> { yield 1; }\n"
        "async function* agen(n: number): AsyncGenerator<number, number, number> {\n"
        "  let t = 0;\n"
        "  for (let i = 0; i < n; i++) t += yield i;\n"
        "  return t;\n"
        "}\n"
        "async function* other(): AsyncGenerator<string> { yield 'x'; }\n"
        "export async function main(): Promise<number> {\n"
        "  let s = 0;\n"
        "  for await (const x of agen(3)) s += x;\n"
        "  const g = agen(2);\n"
        "  await g.next(0);\n"
        "  await g.next(5);\n"
        "  const r = await g.next(6);\n"
        "  for (const y of syncGen()) s += y * 100;\n"
        "  for await (const z of other()) s += z.length * 1000;\n"
        "  return s + r.value * 10;\n"
        "}\n";
    e7::CompileOutput ac = e7::compileSource("ag.ts", kAsyncSrc);
    CHECK(ac.ok);
    if (!ac.ok) {
      fprintf(stderr, "%s", ac.errorText().c_str());
      return 1;
    }
    double av = 0;
    std::string aerr;
    CHECK(run(ac.module, &aerr, &av) == E7_OK);
    CHECK(av == 3 + 110 + 100 + 1000);  // 0+1+2, return 5+6, the sync generator's 1, one string of length 1
    CHECK(ac.module.minVm == 0x000100);  // 0.1.0: everything is baseline
    size_t agi = funcNamed(ac.module, "agen"), ami = funcNamed(ac.module, "main"), asi = funcNamed(ac.module, "syncGen");
    CHECK(agi != SIZE_MAX && ami != SIZE_MAX && asi != SIZE_MAX);
    CHECK(e7::fflagsKind(ac.module.funcs[agi].fflags) == e7::FK_ASYNC_GENERATOR);
    if (failures) return 1;
    auto expectRejectA = [&](const char* what, const std::function<void(e7::Module&)>& f, const char* msg) {
      e7::Module m = ac.module;
      f(m);
      std::string e;
      int st = run(m, &e);
      if (st == E7_OK || e.find(msg) == std::string::npos) {
        fprintf(stderr, "FAIL %s: status %d, error '%s' (expected '%s')\n", what, st, e.c_str(), msg);
        failures++;
      }
    };
    // the first async request in main (the for await's next())
    auto asyncResume = [](const e7::FuncDesc& f) {
      for (size_t pc = 0; pc < f.code.size(); pc += e7::insWords(f.code[pc]))
        if (e7::insOp(f.code[pc]) == e7::OP_RESUME_R && (f.code[pc + 1] & e7::kResumeAsync)) return pc;
      return SIZE_MAX;
    };
    expectRejectA("async generator flags with ASYNC_START",
                  [&](e7::Module& m) { m.funcs[agi].code[0] = e7::encode(e7::OP_ASYNC_START, 0); },
                  "coroutine without its start instruction");
    expectRejectA("async request with RESUME_F", [&](e7::Module& m) {
      auto& f = m.funcs[ami];
      size_t pc = asyncResume(f);
      f.code[pc] = (f.code[pc] & ~0xFFFFull) | e7::OP_RESUME_F;
      // B must be an F register for RESUME_F: then only the metadata is wrong
      uint16_t fr = 0;
      for (size_t r = 0; r < f.regKinds.size(); r++)
        if (f.regKinds[r] == e7::K_F) { fr = (uint16_t)r; break; }
      f.code[pc] = (f.code[pc] & ~(0xFFFFull << 32)) | ((uint64_t)fr << 32);
    }, "bad resume metadata");
    expectRejectA("async request with a class out of range", [&](e7::Module& m) {
      auto& f = m.funcs[ami];
      size_t pc = asyncResume(f);
      f.code[pc + 1] = (f.code[pc + 1] & ~(0xFFFFull << 21)) | ((uint64_t)0xFFF0 << 21);
    }, "bad resume metadata");
    expectRejectA("async request with bits above the value type", [&](e7::Module& m) {
      auto& f = m.funcs[ami];
      size_t pc = asyncResume(f);
      f.code[pc + 1] |= 1ull << 50;
    }, "bad resume metadata");
    expectRejectA("async request with a bad value type", [&](e7::Module& m) {
      auto& f = m.funcs[ami];
      size_t pc = asyncResume(f);
      f.code[pc + 1] = (f.code[pc + 1] & ~(0xFFull << 37)) | (200ull << 37);
    }, "bad resume metadata");
    expectRejectA("async request with a class that is not { value, done }", [&](e7::Module& m) {
      auto& f = m.funcs[ami];
      size_t pc = asyncResume(f);
      uint32_t ci = e7::resumeClass(f.code[pc + 1]);
      m.classes[ci].fields[1].name = "finished";
    }, "bad iterator result class");
    expectRejectA("class bits without the async flag", [&](e7::Module& m) {
      auto& f = m.funcs[ami];
      size_t pc = asyncResume(f);
      f.code[pc + 1] = (f.code[pc + 1] & ~e7::kResumeAsync) | (1ull << 37);
    }, "bad resume metadata");
    // run time: the coroutine is an untyped reference
    expectRejectA("async request to a sync generator", [&](e7::Module& m) {
      // the for...of over syncGen (RESUME_F) made an async request (RESUME_R with the async bits)
      auto& f = m.funcs[ami];
      uint64_t asyncBits = f.code[asyncResume(f) + 1] & ~0xFFFFFull;
      size_t pc = findOp(f, e7::OP_RESUME_F);
      uint16_t rr = 0;
      for (size_t r = 0; r < f.regKinds.size(); r++)
        if (f.regKinds[r] == e7::K_R) { rr = (uint16_t)r; break; }
      f.code[pc] = e7::encode(e7::OP_RESUME_R, e7::insA(f.code[pc]), rr, e7::insC(f.code[pc]));
      f.code[pc + 1] |= asyncBits;
    }, "generator type mismatch");
    expectRejectA("sync resume of an async generator", [&](e7::Module& m) {
      auto& f = m.funcs[ami];
      size_t pc = asyncResume(f);
      f.code[pc + 1] &= 0xFFFFF;  // no async flag and class
      f.code[pc] = (f.code[pc] & ~0xFFFFull) | e7::OP_RESUME_F;
      // B must be an F register now
      uint16_t fr = 0;
      for (size_t r = 0; r < f.regKinds.size(); r++)
        if (f.regKinds[r] == e7::K_F) { fr = (uint16_t)r; break; }
      f.code[pc] = (f.code[pc] & ~(0xFFFFull << 32)) | ((uint64_t)fr << 32);
    }, "generator type mismatch");
    expectRejectA("async request with the wrong value type", [&](e7::Module& m) {
      auto& f = m.funcs[ami];
      size_t pc = asyncResume(f);
      // the class's value field is a number: a reference value type is accepted by the verifier only
      // with a reference field, so make the field a reference too (the generator yields numbers)
      uint32_t ci = e7::resumeClass(f.code[pc + 1]);
      m.classes[ci].fields[0].vt = 10;  // VT_REF
      f.code[pc + 1] = (f.code[pc + 1] & ~(0xFFull << 37)) | (10ull << 37);
    }, "generator type mismatch");
  }

  // run time (security review): a BigInt typed-array native applied to a byte array (the kind is an
  // immediate the bytecode chooses) must not access 8-byte elements of a 1-byte array
  {
    e7::CompileOutput ta = e7::compileSource("t.ts",
                                             "export async function main(): Promise<number> {\n"
                                             "  const a = new BigInt64Array(16);\n"
                                             "  a[15] = 0x4142434445464748n;\n"
                                             "  return Number(a[15]);\n"
                                             "}\n");
    CHECK(ta.ok);
    size_t ti = funcNamed(ta.module, "main");
    bool patched = false;
    for (auto& c : ta.module.funcs[ti].code)
      if (!patched && e7::insOp(c) == e7::OP_LOADI && e7::insB(c) == 9) {
        c = e7::encode(e7::OP_LOADI, e7::insA(c), 0, e7::insC(c));  // TA.new kind 9 (BigInt64Array) -> 0 (Uint8Array)
        patched = true;
      }
    CHECK(patched);
    std::string e;
    CHECK(run(ta.module, &e) == E7_ERR_RUNTIME && e.find("not a BigInt64Array") != std::string::npos);
  }

  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("verify_coroutines_test: all checks passed\n");
  return 0;
}
