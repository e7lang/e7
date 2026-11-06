// Versions: the e7vm release, and the release that introduced each opcode and native function.
//
// A module's `requires` header field is the newest "since" version of anything it uses, so a VM
// can reject a module it cannot run with E7_ERR_VERSION (instead of a confusing verifier error)
// and `e7c --target` can refuse to emit features an older VM lacks.
//
// When you add an opcode or a native function after 0.2.0, add it to opSince() / kNativeSince
// with the version that introduces it. Everything else counts as kBaselineVersion.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

#include "e7/e7.h"
#include "module.h"

namespace e7 {

constexpr uint32_t makeVersion(uint32_t major, uint32_t minor, uint32_t patch) {
  return (major << 16) | (minor << 8) | patch;
}
constexpr uint32_t kVmVersion = makeVersion(E7_VERSION_MAJOR, E7_VERSION_MINOR, E7_VERSION_PATCH);
// Oldest VM that can run format-2 modules.
constexpr uint32_t kBaselineVersion = makeVersion(0, 2, 0);

inline std::string versionString(uint32_t v) {
  char b[32];
  snprintf(b, sizeof b, "%u.%u.%u", (unsigned)(v >> 16), (unsigned)((v >> 8) & 0xFF), (unsigned)(v & 0xFF));
  return b;
}

// Parses "0.2", "0.2.1" or "v0.2"; returns false on malformed input.
inline bool parseVersion(const std::string& s, uint32_t& out) {
  unsigned a = 0, b = 0, c = 0;
  const char* p = s.c_str();
  if (*p == 'v') p++;
  int n = 0;
  char tail = 0;
  int got = sscanf(p, "%u.%u%n", &a, &b, &n);
  if (got != 2) return false;
  p += n;
  if (*p == '.') {
    int m = 0;
    if (sscanf(p, ".%u%n", &c, &m) != 1) return false;
    p += m;
  }
  if (sscanf(p, "%c", &tail) == 1) return false;
  if (a > 255 || b > 255 || c > 255) return false;
  out = makeVersion(a, b, c);
  return true;
}

inline uint32_t opSince(uint16_t op) {
  switch (op) {
    case OP_GEN_START: case OP_ASYNC_START: case OP_YIELD: case OP_RESUME_I: case OP_RESUME_F: case OP_RESUME_R:
    case OP_AWAIT: case OP_FIELD_ABSENT:
      return makeVersion(0, 3, 0);
    default: return kBaselineVersion;
  }
}

struct NativeSince { const char* name; uint32_t since; };
// Natives added after the baseline, e.g. { "json.somethingNew", makeVersion(0, 3, 0) }.
inline const NativeSince* nativeSinceTable(size_t& n) {
  static const NativeSince kTable[] = {
      {"Object.has", makeVersion(0, 3, 0)},
      {"Promise.resolveI", makeVersion(0, 3, 0)}, {"Promise.resolveF", makeVersion(0, 3, 0)},
      {"Promise.resolveR", makeVersion(0, 3, 0)}, {"Promise.reject", makeVersion(0, 3, 0)},
      {"Promise.new", makeVersion(0, 3, 0)}, {"Promise.resolveFnI", makeVersion(0, 3, 0)},
      {"Promise.resolveFnF", makeVersion(0, 3, 0)}, {"Promise.resolveFnR", makeVersion(0, 3, 0)},
      {"Promise.rejectFn", makeVersion(0, 3, 0)}, {"Promise.then", makeVersion(0, 3, 0)},
      {"Promise.finally", makeVersion(0, 3, 0)}, {"Promise.all", makeVersion(0, 3, 0)},
      {"Promise.allSettled", makeVersion(0, 3, 0)}, {"Promise.any", makeVersion(0, 3, 0)},
      {"Promise.race", makeVersion(0, 3, 0)},
      {"test.laterI", makeVersion(0, 3, 0)}, {"test.laterF", makeVersion(0, 3, 0)},
      {"test.laterR", makeVersion(0, 3, 0)}, {"test.laterReject", makeVersion(0, 3, 0)},
  };
  n = sizeof kTable / sizeof kTable[0];
  return kTable;
}

inline uint32_t nativeSince(const std::string& name) {
  size_t n;
  const NativeSince* t = nativeSinceTable(n);
  for (size_t i = 0; i < n; i++)
    if (name == t[i].name) return t[i].since;
  return kBaselineVersion;
}

// Number of code words of the instruction starting with word w (call-like instructions carry
// extra words: see docs/BYTECODE.md).
inline size_t instructionWords(uint64_t w) { return insWords(w); }

// Features that are not an opcode or a native function but change how a VM must treat a module.
// #private class fields: a class (not a record) field named "#x". Since 0.2.5 Object.keys,
// JSON.stringify and console.log skip them (as JavaScript does); older VMs would list them.
constexpr uint32_t kPrivateFieldsSince = makeVersion(0, 2, 5);

// The minimum VM version for a module; `why` names the newest feature used (empty for the baseline).
inline uint32_t moduleRequires(const Module& m, std::string* why = nullptr) {
  uint32_t req = kBaselineVersion;
  if (why) why->clear();
  for (const ClassDesc& c : m.classes) {
    if (c.isRecord) continue;
    for (const FieldDesc& f : c.fields)
      if (!f.name.empty() && f.name[0] == '#' && kPrivateFieldsSince > req) {
        req = kPrivateFieldsSince;
        if (why) *why = "#private class fields";
      }
  }
  // format 3 (0.3): coroutines, records with a presence mask, regular expressions
  constexpr uint32_t v030 = makeVersion(0, 3, 0);
  if (req < v030) {
    for (const ClassDesc& c : m.classes)
      if (c.cflags && req < v030) { req = v030; if (why) *why = "objects with optional properties (presence mask)"; }
    for (const FuncDesc& f : m.funcs)
      if (f.fflags && req < v030) { req = v030; if (why) *why = fflagsKind(f.fflags) == FK_GENERATOR ? "generators" : "async functions"; }
    if (!m.regexps.empty() && req < v030) { req = v030; if (why) *why = "regular expressions"; }
  }
  for (const NativeImport& n : m.natives) {
    uint32_t s = nativeSince(n.name);
    if (s > req) { req = s; if (why) *why = "native function " + n.name; }
  }
  for (const FuncDesc& f : m.funcs)
    for (size_t pc = 0; pc < f.code.size(); pc += instructionWords(f.code[pc])) {
      uint16_t op = insOp(f.code[pc]);
      if (op >= OP__COUNT) break;  // malformed: the verifier reports it
      uint32_t s = opSince(op);
      if (s > req) { req = s; if (why) *why = std::string("instruction ") + kOpInfo[op].name; }
    }
  return req;
}

}  // namespace e7
