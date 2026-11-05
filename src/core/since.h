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
    // case OP_NEWTHING: return makeVersion(0, 3, 0);
    default: return kBaselineVersion;
  }
}

struct NativeSince { const char* name; uint32_t since; };
// Natives added after the baseline, e.g. { "json.somethingNew", makeVersion(0, 3, 0) }.
inline const NativeSince* nativeSinceTable(size_t& n) {
  static const NativeSince kTable[] = {{"", 0}};
  n = 0;
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
inline size_t instructionWords(uint64_t w) {
  uint16_t op = insOp(w), c = insC(w);
  switch (op) {
    case OP_CALL: case OP_CALLN: case OP_CLOSURE: return 1 + (c + 3u) / 4;
    case OP_CALLV: case OP_CALLM: case OP_CALLI: return 2 + (c + 3u) / 4;
    default: return 1;
  }
}

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
