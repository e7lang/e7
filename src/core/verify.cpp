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
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  auto F = [&](const char* fmt, auto... args) {
    snprintf(buf, sizeof buf, fmt, args...);
    return v.fail(buf);
  };
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

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
  // host signatures of exports drive how the C API converts arguments/results, so they must agree
  // exactly with the function's register kinds
  auto hostKind = [](char c) -> char {
    switch (c) {
      case 'n': return 'F';
      case 'b': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': return 'I';
      case 's': case 'y': case 'h': return 'R';
      case 'v': return 'V';
      case 'x': return '*';  // not passable from/to the host
      default: return 0;
    }
  };
  for (auto& e : m.exports) {
    if (e.func >= m.funcs.size()) return F("export '%s' has invalid function", e.name.c_str());
    if (m.funcs[e.func].sig >= m.sigs.size()) return F("export '%s' has invalid function", e.name.c_str());
    const std::string& fs = m.sigs[m.funcs[e.func].sig];
    const std::string fp = sigParams(fs);
    const std::string& hs = e.hostSig;
    size_t gt = hs.find('>');
    if (gt == std::string::npos || gt + 2 != hs.size() || gt != fp.size())
      return F("export '%s' has invalid host signature", e.name.c_str());
    for (size_t i = 0; i <= gt; i++) {
      char hk = i < gt ? hostKind(hs[i]) : hostKind(hs[gt + 1]);
      char fk = i < gt ? fp[i] : sigRet(fs);
      if (!hk || (hk == 'V') != (fk == 'V') || (hk != '*' && hk != fk))
        return F("export '%s' host signature does not match the function", e.name.c_str());
    }
  }

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
    // coroutines (format 3): generators yield a value of a fixed kind and return nothing or that
    // kind; async functions neither yield nor receive; both return a reference (the coroutine or
    // the promise) to their caller
    uint8_t fk = fflagsKind(f.fflags);
    if (fk == FK_PLAIN && f.fflags != 0) return F("function %zu: bad function flags", fi);
    if (fk != FK_PLAIN && sigRet(m.sigs[f.sig]) != 'R') return F("function %zu: coroutine must return a reference", fi);
    if (fk == FK_ASYNC && (fflagsYield(f.fflags) || fflagsNext(f.fflags))) return F("function %zu: bad function flags", fi);
    if ((fk == FK_GENERATOR || fk == FK_ASYNC_GENERATOR) &&
        (fflagsYield(f.fflags) == 0 || (fflagsReturn(f.fflags) != 0 && fflagsReturn(f.fflags) != fflagsYield(f.fflags))))
      return F("function %zu: bad generator flags", fi);
  }
  if (m.sigs[m.funcs[m.initFunc].sig] != ">V") return F("init function must take no arguments");

  // classes
  for (size_t ci = 0; ci < m.classes.size(); ci++) {
    const ClassDesc& c = m.classes[ci];
    if (c.parent >= (int32_t)ci || c.parent < -1) return F("class %zu: invalid parent", ci);
    if (c.fields.size() > 65535) return F("class %zu: too many fields", ci);
    if (c.isRecord > 2) return F("class %zu: bad kind", ci);
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
    const uint8_t fkind = fflagsKind(f.fflags);
    // RET_* in a coroutine returns the body's value (fflags), not the function's result
    const char retKind = fkind == FK_PLAIN ? sigRet(m.sigs[f.sig]) : "VIFR"[fflagsReturn(f.fflags)];
    const uint16_t startOp = fkind == FK_PLAIN ? OP__COUNT : fkind == FK_ASYNC ? OP_ASYNC_START : OP_GEN_START;
    if (startOp != OP__COUNT && (f.code.empty() || insOp(f.code[0]) != startOp))
      return F("function %zu: coroutine without its start instruction", fi);
    if (n == 0) return F("function %zu: empty code", fi);
    if (n > 0x7FFFFFFF) return F("function %zu: code too large", fi);
    std::vector<uint8_t> isStart(n, 0);
    std::vector<uint32_t> jumps;
    size_t pc = 0;
    size_t lastStart = 0;
    // each inline-cache slot serves exactly one role (field of kind I/F/R, or method), so a cache hit
    // filled by one instruction can never be misread by another
    std::vector<char> icRole(f.icNames.size(), 0);
    auto icUse = [&](uint32_t slot, char role) -> bool {
      if (icRole[slot] && icRole[slot] != role) return F("function %zu pc %zu: inline cache reused with a different type", fi, pc);
      icRole[slot] = role;
      return true;
    };
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
            if (op == OP_CALLI && !icUse(lo, 'M')) return false;
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
          case OP_YIELD: {
            // A yielded value, B received value (or none), C mode; extra word: return-value register
            if (fkind != FK_GENERATOR && fkind != FK_ASYNC_GENERATOR) return F("function %zu pc %zu: yield outside a generator", fi, pc);
            if (next >= n) return F("function %zu pc %zu: truncated yield", fi, pc);
            uint64_t ex = code[next];
            uint16_t rr = (uint16_t)ex;
            if ((ex >> 16) != 0) return F("function %zu pc %zu: bad yield metadata", fi, pc);
            uint8_t nk = fflagsNext(f.fflags), rk = fflagsReturn(f.fflags);
            if (!reg(a, "?IFR"[fflagsYield(f.fflags)]) || !reg(c, 'I')) return false;
            if (b != kNoReg && (nk == 0 || !reg(b, "?IFR"[nk]))) return F("function %zu pc %zu: bad yield receive register", fi, pc);
            if (rr != kNoReg && (rk == 0 || !reg(rr, "?IFR"[rk]))) return F("function %zu pc %zu: bad yield return register", fi, pc);
            next++;
            break;
          }
          case OP_RESUME_I:
          case OP_RESUME_F:
          case OP_RESUME_R: {
            char vk = op == OP_RESUME_I ? 'I' : op == OP_RESUME_F ? 'F' : 'R';
            if (!reg(a, 'R') || !reg(b, vk) || !reg(c, 'I')) return false;
            if (next >= n) return F("function %zu pc %zu: truncated resume", fi, pc);
            uint64_t ex = code[next];
            uint16_t sr = (uint16_t)ex;
            uint32_t mode = (uint32_t)(ex >> 16) & 3, sk = (uint32_t)(ex >> 18) & 3;
            if ((ex >> 20) != 0 || mode == 3) return F("function %zu pc %zu: bad resume metadata", fi, pc);
            if (sk == 0 ? sr != kNoReg : !reg(sr, "?IFR"[sk])) return F("function %zu pc %zu: bad resume value register", fi, pc);
            if (mode == 1 && sk != 3) return F("function %zu pc %zu: throw() needs an error object", fi, pc);
            next++;
            break;
          }
          case OP_AWAIT: {
            // A destination (any kind, or none), B the promise
            if (fkind != FK_ASYNC && fkind != FK_ASYNC_GENERATOR) return F("function %zu pc %zu: await outside an async function", fi, pc);
            if ((a != kNoReg && !reg(a, '*')) || !reg(b, 'R')) return false;
            if (c != 0) return F("function %zu pc %zu: nonzero unused operand", fi, pc);
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
            case 'j': {
              int64_t t = (int64_t)pc + 1 + (int16_t)x;
              if (t < 0 || t >= (int64_t)n) return F("function %zu pc %zu: jump out of range", fi, pc);
              jumps.push_back((uint32_t)t);
              break;
            }
            case 'c':
              if (x >= m.classes.size()) return F("function %zu pc %zu: bad class", fi, pc);
              break;
            case 'Q':
              if (x >= f.icNames.size()) return F("function %zu pc %zu: bad inline cache", fi, pc);
              if (!icUse(x, oi.kind)) return false;
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
        if ((op == OP_GEN_START || op == OP_ASYNC_START) && (pc != 0 || op != startOp))
          return F("function %zu pc %zu: misplaced coroutine start", fi, pc);
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
    // try/catch handlers: ranges and targets at instruction starts, a reference register for the
    // error, inner handlers listed before the ones enclosing them, ranges nested or disjoint
    if (f.handlers.size() > 65535) return F("function %zu: too many handlers", fi);
    for (size_t k = 0; k < f.handlers.size(); k++) {
      const HandlerDesc& h = f.handlers[k];
      if (h.start >= h.end || h.end > n || !isStart[h.start] || (h.end < n && !isStart[h.end]))
        return F("function %zu: bad handler range", fi);
      // after the range: a catch can never re-enter code before the error without a charged back edge
      if (h.handler >= n || h.handler < h.end || !isStart[h.handler]) return F("function %zu: bad handler target", fi);
      if (h.reg >= f.regKinds.size() || f.regKinds[h.reg] != K_R) return F("function %zu: bad handler register", fi);
      for (size_t j = 0; j < k; j++) {
        const HandlerDesc& g = f.handlers[j];  // listed earlier: inside h, or disjoint from it
        bool disjoint = g.end <= h.start || h.end <= g.start;
        bool inside = h.start <= g.start && g.end <= h.end;
        if (!disjoint && !inside) return F("function %zu: overlapping handlers", fi);
      }
    }
    // debug information: only positions, names and register mappings, but the debugger reads
    // registers through it, so every index and range is checked
    if (f.hasDebug) {
      const FuncDebug& d = f.debug;
      for (size_t k = 0; k < d.pos.size(); k++)
        if (d.pos[k].pc >= n || (k && d.pos[k].pc <= d.pos[k - 1].pc)) return F("function %zu: bad debug position table", fi);
      for (size_t k = 0; k < d.stmts.size(); k++)
        if (d.stmts[k].pc >= n || !isStart[d.stmts[k].pc] || (k && d.stmts[k].pc <= d.stmts[k - 1].pc))
          return F("function %zu: bad debug statement table", fi);
      for (const DebugLocal& l : d.locals) {
        if (l.reg >= f.regKinds.size()) return F("function %zu: debug variable register out of range", fi);
        if (l.startPc > l.endPc || l.endPc > n) return F("function %zu: bad debug variable range", fi);
        if (l.vt >= VT__COUNT || (l.flags & ~(kDebugLocalCell | kDebugLocalParam)))
          return F("function %zu: bad debug variable type", fi);
        uint8_t want = (l.flags & kDebugLocalCell) ? (uint8_t)K_R : (uint8_t)kindOfVT(l.vt);
        if (f.regKinds[l.reg] != want) return F("function %zu: debug variable kind mismatch", fi);
      }
      if (!d.upvalNames.empty() && d.upvalNames.size() != f.upvalKinds.size())
        return F("function %zu: bad debug upvalue names", fi);
    }
    uint16_t lastOp = insOp(code[lastStart]);
    if (lastOp != OP_RET_I && lastOp != OP_RET_F && lastOp != OP_RET_R && lastOp != OP_RET_V &&
        lastOp != OP_JMP && lastOp != OP_ABORT && lastOp != OP_THROW)
      return F("function %zu: code may fall off the end", fi);
  }
  return true;
}

}  // namespace e7
