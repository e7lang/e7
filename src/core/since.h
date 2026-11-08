// Versions: the e7vm release, and the release that introduced each opcode and native function.
//
// A module's `requires` header field is the newest "since" version of anything it uses, so a VM
// can reject a module it cannot run with E7_ERR_VERSION (instead of a confusing verifier error).
//
// Everything in 0.1.0 counts as kBaselineVersion, so the tables below are empty. When a later
// release adds an opcode, add a case to opSince() returning the version that introduces it; when it
// adds a native function, add { "Name.fn", makeVersion(x, y, z) } to nativeSinceTable(). A feature
// that is neither (a new class or function flag, a new instruction operand bit) gets a check in
// moduleRequires().
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
// The version of everything that has no entry below: the first release.
constexpr uint32_t kBaselineVersion = makeVersion(0, 1, 0);

inline std::string versionString(uint32_t v) {
  char b[32];
  snprintf(b, sizeof b, "%u.%u.%u", (unsigned)(v >> 16), (unsigned)((v >> 8) & 0xFF), (unsigned)(v & 0xFF));
  return b;
}

inline uint32_t opSince(uint16_t op) {
  (void)op;  // no entries yet; e.g. if (op == OP_SOMETHING_NEW) return makeVersion(x, y, z);
  return kBaselineVersion;
}

struct NativeSince { const char* name; uint32_t since; };
// Natives added after the baseline, e.g. { "JSON.somethingNew", makeVersion(x, y, z) }.
inline const NativeSince* nativeSinceTable(size_t& n) {
  static const NativeSince kTable[] = {
      {"JSON.parse2", makeVersion(0, 1, 1)},     // typed JSON.parse with literal types / enums ('L' descriptors)
      {"JSON.tryParse2", makeVersion(0, 1, 1)},
      {"TA.sortCmp", makeVersion(0, 1, 2)},      // typedArray.sort(cmp) in place
      {nullptr, 0},  // placeholder (C++ has no empty arrays); not an entry
  };
  n = sizeof kTable / sizeof kTable[0] - 1;
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

// The minimum VM version for a module; `why` names the newest feature used (empty for the baseline).
inline uint32_t moduleRequires(const Module& m, std::string* why = nullptr) {
  uint32_t req = kBaselineVersion;
  if (why) why->clear();
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
