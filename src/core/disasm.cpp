#include <cstdio>

#include "module.h"

namespace e7 {

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-security"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
std::string disassemble(const Module& m) {
  std::string out;
  char buf[512];
  auto add = [&](const char* fmt, auto... a) { snprintf(buf, sizeof buf, fmt, a...); out += buf; };
  add("module %s (requires e7vm %u.%u.%u)\n", m.name.c_str(), (unsigned)(m.minVm >> 16), (unsigned)((m.minVm >> 8) & 0xFF),
      (unsigned)(m.minVm & 0xFF));
  for (size_t i = 0; i < m.natives.size(); i++)
    add("  native #%zu %s %s\n", i, m.natives[i].name.c_str(), m.sigs[m.natives[i].sig].c_str());
  for (size_t i = 0; i < m.globals.size(); i++) add("  global #%zu %s vt=%u\n", i, m.globals[i].name.c_str(), m.globals[i].vt);
  for (size_t i = 0; i < m.classes.size(); i++) {
    auto& c = m.classes[i];
    add("  class #%zu %s parent=%d fields=%zu methods=%zu\n", i, c.name.c_str(), c.parent, c.fields.size(), c.vtable.size());
  }
  for (size_t fi = 0; fi < m.funcs.size(); fi++) {
    auto& f = m.funcs[fi];
    std::string kinds;
    for (auto k : f.regKinds) kinds += "IFR"[k % 3];
    add("\nfunc #%zu %s sig=%s regs=%s up=%zu\n", fi, f.name.c_str(),
        f.sig < m.sigs.size() ? m.sigs[f.sig].c_str() : "?", kinds.c_str(), f.upvalKinds.size());
    for (auto& h : f.handlers) add("  try [%u, %u) -> %u, error in r%u\n", h.start, h.end, h.handler, h.reg);
    size_t pc = 0;
    while (pc < f.code.size()) {
      uint64_t w = f.code[pc];
      uint16_t op = insOp(w);
      if (op >= OP__COUNT) { add("  %4zu  ??? %u\n", pc, op); pc++; continue; }
      const OpInfo& oi = kOpInfo[op];
      add("  %4zu  %-10s", pc, oi.name);
      size_t extra = 0;
      if (oi.fmt[0] == '*') {
        uint16_t c = insC(w);
        bool meta = op == OP_CALLV || op == OP_CALLM || op == OP_CALLI;
        if (op == OP_YIELD || op == OP_RESUME_I || op == OP_RESUME_F || op == OP_RESUME_R || op == OP_AWAIT) {
          add(" %u, %u, %u", insA(w), insB(w), c);
          if (insWords(w) > 1 && pc + 1 < f.code.size()) add(" ex=%llx", (unsigned long long)f.code[pc + 1]);
          out += "\n";
          pc += insWords(w);
          continue;
        }
        add(" a=%u b=%u n=%u", insA(w), insB(w), c);
        if (meta && pc + 1 < f.code.size()) add(" meta=%llx", (unsigned long long)f.code[pc + 1]);
        extra = (meta ? 1 : 0) + (c + 3) / 4;
        add(" [");
        for (uint32_t k = 0; k < c; k++) {
          size_t at = pc + 1 + (meta ? 1 : 0) + k / 4;
          if (at < f.code.size()) add("%s%u", k ? " " : "", (unsigned)(uint16_t)(f.code[at] >> (16 * (k % 4))));
        }
        add("]");
      } else if (oi.fmt[2] == '*') {
        add(" %u, %u", insA(w), insBx(w));
      } else if (oi.fmt[2] == 'j') {
        add(" %u, %u -> %lld", insA(w), insB(w), (long long)pc + 1 + (int16_t)insC(w));
      } else {
        add(" %u, %u, %u", insA(w), insB(w), insC(w));
      }
      out += "\n";
      pc += 1 + extra;
    }
  }
  return out;
}

}  // namespace e7
