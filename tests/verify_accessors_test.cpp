// Getters and setters in object literals (0.3.1): records with the accessors class flag. The verifier
// rejects the flag on classes that are not records and accessor fields that are not references; the
// natives that call getters (JSON.stringify, e7:test's deepEqual) check the accessor pair at run time,
// so a malformed one is a clean runtime error; the module requires e7vm 0.3.1 (--target 0.3.0 refuses it).
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "compiler/compiler.h"
#include "core/module.h"
#include "core/since.h"
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
    "interface Sized { get size(): number; set size(v: number) }\n"
    "export function main(): number {\n"
    "  let n = 2;\n"
    "  const o = { a: 1, get double(): number { return this.a * 2; }, get flag(): boolean { return this.a > 0; } };\n"
    "  const s: Sized = { get size() { return n; }, set size(v) { n = v; } };\n"
    "  s.size = s.size + 5;\n"
    "  const json = JSON.stringify(o);\n"
    "  return o.double * 100 + s.size + json.length * 1000;\n"
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

int main() {
  e7::CompileOutput co = e7::compileSource("a.ts", kSrc);
  CHECK(co.ok);
  if (!co.ok) {
    fprintf(stderr, "%s", co.errorText().c_str());
    return 1;
  }
  double v = 0;
  std::string err;
  CHECK(run(co.module, &err, &v) == E7_OK);
  // {"a":1,"double":2,"flag":true}: 30 characters; double 2; size 2 + 5
  CHECK(v == 30 * 1000 + 2 * 100 + 7);
  std::string why;
  CHECK(e7::moduleRequires(co.module, &why) == e7::makeVersion(0, 3, 1));
  CHECK(why == "getters and setters in object literals");
  CHECK(e7::neededFormat(co.module) == 3);
  // the records with accessors, and the accessor pair class of `double` ([getter, setter, number])
  int rec = -1, pairNum = -1;
  for (size_t i = 0; i < co.module.classes.size(); i++) {
    const e7::ClassDesc& c = co.module.classes[i];
    if ((c.cflags & e7::kClassFlagAccessors) && rec < 0) rec = (int)i;
    if (c.isRecord == 2 && c.fields.size() == 3 && c.fields[2].vt == e7::VT_NUMBER && pairNum < 0) pairNum = (int)i;
  }
  CHECK(rec >= 0 && pairNum >= 0);
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
  expectReject("accessors flag on a class that is not a record",
               [&](e7::Module& m) { m.classes[(size_t)rec].isRecord = 0; }, "bad class flags");
  expectReject("accessor field that is not a reference", [&](e7::Module& m) {
    for (auto& f : m.classes[(size_t)rec].fields)
      if (f.name == "double") f.vt = e7::VT_NUMBER;
  }, "is not a reference");
  // run time: the accessor pair's value slot says the getter returns a reference (it returns a number)
  expectReject("accessor pair with another value kind",
               [&](e7::Module& m) { m.classes[(size_t)pairNum].fields[2].vt = e7::VT_REF; }, "malformed accessor property");
  // run time: the accessor pair is not a tuple
  expectReject("accessor pair that is not a tuple",
               [&](e7::Module& m) { m.classes[(size_t)pairNum].isRecord = 1; }, "malformed accessor property");

  // --target 0.3.0 refuses the feature
  e7::CompileOptions opts;
  opts.target = e7::makeVersion(0, 3, 0);
  e7::CompileOutput old = e7::compileSource("a.ts", kSrc, opts);
  CHECK(!old.ok && old.errorText().find("getters and setters in object literals") != std::string::npos);

  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("verify_accessors_test: all checks passed\n");
  return 0;
}
