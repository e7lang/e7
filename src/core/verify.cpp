// Bytecode verifier: guarantees that a module cannot violate memory safety.
// Register kinds are fixed per function, so every operand kind can be checked
// statically; object shapes are re-checked at run time by the interpreter.
#include <cstdio>
#include <set>

#include "module.h"

namespace e7 {

namespace {

struct V {
  const Module& m;
  std::string& err;
  bool fail(const std::string& msg) {
    if (err.empty()) err = msg;
    return false;
  }
};

bool validSig(const std::string& s) {
  size_t gt = s.find('>');
  if (gt == std::string::npos || gt + 2 != s.size()) return false;
  for (size_t i = 0; i < gt; i++)
    if (s[i] != 'I' && s[i] != 'F' && s[i] != 'R') return false;
  char r = s.back();
  return r == 'I' || r == 'F' || r == 'R' || r == 'V';
}

char kc(uint8_t k) { return k < 3 ? "IFR"[k] : '?'; }

}  // namespace

bool verifyModule(const Module& m, std::string& err) {
  err.clear();
  V v{m, err};
  char buf[256];
  auto F = [&](const char* fmt, auto... args) {
    snprintf(buf, sizeof buf, fmt, args...);
    return v.fail(buf);
  };

  for (size_t i = 0; i < m.sigs.size(); i++)
    if (!validSig(m.sigs[i])) return F("invalid signature #%zu", i);
  for (auto& n : m.natives)
    if (n.sig >= m.sigs.size()) return F("native '%s' has invalid signature", n.name.c_str());
  for (auto& g : m.globals)
    if (g.vt >= VT__COUNT) return F("global '%s' has invalid type", g.name.c_str());
  if (m.funcs.empty()) return F("module has no functions");
  if (m.initFunc >= m.funcs.size()) return F("invalid init function");
  if (m.funcs.size() > 65535) return F("too many functions");
  if (m.classes.size() > 65535) return F("too many classes");
  if (m.natives.size() > 65535) return F("too many natives");
  for (auto& e : m.exports)
    if (e.func >= m.funcs.size()) return F("export '%s' has invalid function", e.name.c_str());

  // functions: signatures and register files
  for (size_t fi = 0; fi < m.funcs.size(); fi++) {
    const FuncDesc& f = m.funcs[fi];
    if (f.sig >= m.sigs.size()) return F("function %zu: invalid signature", fi);
    const std::string params = sigParams(m.sigs[f.sig]);
    if (params.size() != f.nparams) return F("function %zu: parameter count mismatch", fi);
    if (f.regKinds.size() > 65535 || f.nparams > f.regKinds.size()) return F("function %zu: bad register count", fi);
    for (uint8_t k : f.regKinds)
      if (k > K_R) return F("function %zu: bad register kind", fi);
    for (size_t p = 0; p < f.nparams; p++)
      if (kc(f.regKinds[p]) != params[p]) return F("function %zu: parameter kind mismatch", fi);
    for (uint8_t k : f.upvalKinds)
      if (k > K_R) return F("function %zu: bad upvalue kind", fi);
    if (f.upvalKinds.size() > 0x7FFF) return F("function %zu: too many upvalues", fi);
    for (uint32_t s : f.icNames)
      if (s >= m.strs.size()) return F("function %zu: bad inline cache name", fi);
    if (f.lines.size() % 2) return F("function %zu: bad line table", fi);
  }
  if (m.sigs[m.funcs[m.initFunc].sig] != ">V") return F("init function must take no arguments");

  // classes
  for (size_t ci = 0; ci < m.classes.size(); ci++) {
    const ClassDesc& c = m.classes[ci];
    if (c.parent >= (int32_t)ci || c.parent < -1) return F("class %zu: invalid parent", ci);
    if (c.fields.size() > 65535) return F("class %zu: too many fields", ci);
    for (auto& fd : c.fields)
      if (fd.vt >= VT__COUNT) return F("class %zu: bad field type", ci);
    if (c.methodNames.size() != c.vtable.size()) return F("class %zu: bad method table", ci);
    for (uint32_t fn : c.vtable) {
      if (fn >= m.funcs.size()) return F("class %zu: bad method", ci);
      const std::string& s = m.sigs[m.funcs[fn].sig];
      if (s.empty() || s[0] != 'R') return F("class %zu: method without receiver", ci);
    }
    if (c.parent >= 0) {
      const ClassDesc& p = m.classes[c.parent];
      if (c.fields.size() < p.fields.size() || c.vtable.size() < p.vtable.size())
        return F("class %zu: layout does not extend parent", ci);
      for (size_t k = 0; k < p.fields.size(); k++)
        if (p.fields[k].vt != c.fields[k].vt) return F("class %zu: inherited field type changed", ci);
      for (size_t k = 0; k < p.vtable.size(); k++)
        if (m.funcs[p.vtable[k]].sig != m.funcs[c.vtable[k]].sig &&
            m.sigs[m.funcs[p.vtable[k]].sig] != m.sigs[m.funcs[c.vtable[k]].sig])
          return F("class %zu: override signature mismatch", ci);
    }
  }

  // code
  for (size_t fi = 0; fi < m.funcs.size(); fi++) {
    const FuncDesc& f = m.funcs[fi];
    const auto& code = f.code;
    const size_t n = code.size();
    const char retKind = sigRet(m.sigs[f.sig]);
    if (n == 0) return F("function %zu: empty code", fi);
    if (n > 0x7FFFFFFF) return F("function %zu: code too large", fi);
    std::vector<uint8_t> isStart(n, 0);
    std::vector<uint32_t> jumps;
    size_t pc = 0;
    size_t lastStart = 0;
    auto reg = [&](uint32_t r, char kind) -> bool {
      if (r >= f.regKinds.size()) return F("function %zu pc %zu: register %u out of range", fi, pc, r);
      if (kind != '*' && kc(f.regKinds[r]) != kind)
        return F("function %zu pc %zu: register %u kind %c, expected %c", fi, pc, r, kc(f.regKinds[r]), kind);
      return true;
    };
    auto dstOk = [&](uint32_t a, char ret) -> bool {
      if (a == kNoReg) return true;
      if (ret == 'V') return F("function %zu pc %zu: void call with destination", fi, pc);
      return reg(a, ret);
    };
    auto argsOk = [&](size_t at, uint32_t argc, const std::string& kinds) -> bool {
      size_t words = (argc + 3) / 4;
      if (at + words > n) return F("function %zu pc %zu: truncated argument list", fi, pc);
      for (uint32_t k = 0; k < argc; k++) {
        uint16_t r = (uint16_t)(code[at + k / 4] >> (16 * (k % 4)));
        if (!reg(r, kinds[k])) return false;
      }
      // unused argument slots must be zero
      for (uint32_t k = argc; k < words * 4; k++)
        if ((uint16_t)(code[at + k / 4] >> (16 * (k % 4))) != 0) return F("function %zu pc %zu: garbage in argument list", fi, pc);
      return true;
    };
    while (pc < n) {
      lastStart = pc;
      isStart[pc] = 1;
      uint64_t w = code[pc];
      uint16_t op = insOp(w);
      if (op >= OP__COUNT) return F("function %zu pc %zu: invalid opcode %u", fi, pc, op);
      const OpInfo& oi = kOpInfo[op];
      uint16_t a = insA(w), b = insB(w), c = insC(w);
      uint32_t bx = insBx(w);
      size_t next = pc + 1;
      if (oi.fmt[0] == '*') {
        switch (op) {
          case OP_CALL: {
            if (b >= m.funcs.size()) return F("function %zu pc %zu: bad function index", fi, pc);
            const std::string& s = m.sigs[m.funcs[b].sig];
            std::string ps = sigParams(s);
            if (c != ps.size()) return F("function %zu pc %zu: argument count mismatch", fi, pc);
            if (!dstOk(a, sigRet(s)) || !argsOk(next, c, ps)) return false;
            next += (c + 3) / 4;
            break;
          }
          case OP_CALLN: {
            if (b >= m.natives.size()) return F("function %zu pc %zu: bad native index", fi, pc);
            const std::string& s = m.sigs[m.natives[b].sig];
            std::string ps = sigParams(s);
            if (c != ps.size()) return F("function %zu pc %zu: native argument count mismatch", fi, pc);
            if (!dstOk(a, sigRet(s)) || !argsOk(next, c, ps)) return false;
            next += (c + 3) / 4;
            break;
          }
          case OP_CALLV:
          case OP_CALLM:
          case OP_CALLI: {
            if (!reg(b, 'R')) return false;
            if (next >= n) return F("function %zu pc %zu: truncated call", fi, pc);
            uint64_t ex = code[next];
            uint32_t lo = (uint32_t)ex, hi = (uint32_t)(ex >> 32);
            uint32_t sig = op == OP_CALLV ? lo : hi;
            if (op == OP_CALLV && hi != 0) return F("function %zu pc %zu: bad call metadata", fi, pc);
            if (op == OP_CALLI && lo >= f.icNames.size()) return F("function %zu pc %zu: bad inline cache", fi, pc);
            if (sig >= m.sigs.size()) return F("function %zu pc %zu: bad call signature", fi, pc);
            const std::string& s = m.sigs[sig];
            std::string ps = sigParams(s);
            if (op != OP_CALLV) {
              if (ps.empty() || ps[0] != 'R') return F("function %zu pc %zu: method signature without receiver", fi, pc);
              ps = ps.substr(1);
            }
            if (c != ps.size()) return F("function %zu pc %zu: argument count mismatch", fi, pc);
            next++;
            if (!dstOk(a, sigRet(s)) || !argsOk(next, c, ps)) return false;
            next += (c + 3) / 4;
            break;
          }
          case OP_CLOSURE: {
            if (!reg(a, 'R')) return false;
            if (b >= m.funcs.size()) return F("function %zu pc %zu: bad closure function", fi, pc);
            const FuncDesc& target = m.funcs[b];
            if (c != target.upvalKinds.size()) return F("function %zu pc %zu: capture count mismatch", fi, pc);
            size_t words = (c + 3) / 4;
            if (next + words > n) return F("function %zu pc %zu: truncated capture list", fi, pc);
            for (uint32_t k = 0; k < c; k++) {
              uint16_t spec = (uint16_t)(code[next + k / 4] >> (16 * (k % 4)));
              if (spec & 0x8000) {
                uint32_t ui = spec & 0x7FFF;
                if (ui >= f.upvalKinds.size() || f.upvalKinds[ui] != target.upvalKinds[k])
                  return F("function %zu pc %zu: bad upvalue capture", fi, pc);
              } else if (!reg(spec, 'R')) {
                return false;
              }
            }
            next += words;
            break;
          }
          default:
            return F("function %zu pc %zu: bad special opcode", fi, pc);
        }
      } else {
        bool bxForm = oi.fmt[2] == '*';
        uint16_t ops[3] = {a, b, c};
        int cnt = bxForm ? 1 : 3;
        for (int k = 0; k < cnt; k++) {
          char t = oi.fmt[k];
          uint16_t x = ops[k];
          switch (t) {
            case 'I': case 'F': case 'R':
              if (!reg(x, t)) return false;
              break;
            case '_':
              if (x != 0) return F("function %zu pc %zu: nonzero unused operand", fi, pc);
              break;
            case 'v':
              if (x >= VT__COUNT) return F("function %zu pc %zu: bad value type", fi, pc);
              break;
            case 'n': case 's': case 'e':
              break;
            case 'c':
              if (x >= m.classes.size()) return F("function %zu pc %zu: bad class", fi, pc);
              break;
            case 'Q':
              if (x >= f.icNames.size()) return F("function %zu pc %zu: bad inline cache", fi, pc);
              break;
            case 'U':
              if (x >= f.upvalKinds.size()) return F("function %zu pc %zu: bad upvalue", fi, pc);
              if (oi.kind != '-' && kc(f.upvalKinds[x]) != oi.kind) return F("function %zu pc %zu: upvalue kind mismatch", fi, pc);
              break;
            default:
              return F("function %zu pc %zu: internal format error", fi, pc);
          }
        }
        if (bxForm) {
          switch (oi.fmt[1]) {
            case 'J':
              if (bx >= n) return F("function %zu pc %zu: jump out of range", fi, pc);
              jumps.push_back(bx);
              break;
            case 'k': if (bx >= m.ints.size()) return F("function %zu pc %zu: bad int constant", fi, pc); break;
            case 'd': if (bx >= m.nums.size()) return F("function %zu pc %zu: bad number constant", fi, pc); break;
            case 't': if (bx >= m.strs.size()) return F("function %zu pc %zu: bad string constant", fi, pc); break;
            case 'g':
              if (bx >= m.globals.size()) return F("function %zu pc %zu: bad global", fi, pc);
              if (kc(kindOfVT(m.globals[bx].vt)) != oi.kind) return F("function %zu pc %zu: global kind mismatch", fi, pc);
              break;
            case 'm': break;
            default: return F("function %zu pc %zu: internal format error", fi, pc);
          }
        }
        // opcode-specific extra rules
        if (op == OP_BOX_I && kindOfVT((uint8_t)c) != K_I) return F("function %zu pc %zu: BOX_I with non-int type", fi, pc);
        if (op == OP_INT2STR && kindOfVT((uint8_t)c) != K_I) return F("function %zu pc %zu: INT2STR with non-int type", fi, pc);
        if (op == OP_RET_I || op == OP_RET_F || op == OP_RET_R || op == OP_RET_V) {
          char rk = op == OP_RET_I ? 'I' : op == OP_RET_F ? 'F' : op == OP_RET_R ? 'R' : 'V';
          if (rk != retKind) return F("function %zu pc %zu: return kind mismatch", fi, pc);
        }
      }
      pc = next;
    }
    for (uint32_t t : jumps)
      if (!isStart[t]) return F("function %zu: jump into the middle of an instruction", fi);
    uint16_t lastOp = insOp(code[lastStart]);
    if (lastOp != OP_RET_I && lastOp != OP_RET_F && lastOp != OP_RET_R && lastOp != OP_RET_V &&
        lastOp != OP_JMP && lastOp != OP_ABORT)
      return F("function %zu: code may fall off the end", fi);
  }
  return true;
}

}  // namespace e7
