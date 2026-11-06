// The public entry points and the matcher: a loop over the program with two explicit stacks, the
// choice points (where to resume on failure) and an undo log of register writes. Backtracking to a
// choice point undoes the writes made after it, so captures, loop counters and positions are restored
// without copying them; a lookaround that succeeds cuts the choice points of its body (it is atomic)
// and keeps their writes.
#include <algorithm>
#include <cstring>
#include <string>

#include "regexp/regexp_impl.h"

namespace e7::re {
namespace {

struct Choice {
  int32_t pc;   // >= 0: resume there; kBarrier; or a star choice (starChoice(pc of the OP_STAR))
  int32_t pos;
  int32_t aux;  // OP_STAR: greedy, the position after the minimum; lazy, the count so far
  uint32_t undo;  // undo log height when the choice was made
};
struct Undo {
  int32_t reg, old;
};
constexpr int32_t kBarrier = -1;  // positive lookaround: failing into it keeps failing
inline int32_t starChoice(int32_t pc) { return -2 - pc; }

inline bool isWordChar(char16_t c, bool extended) {
  return isBasicWordChar(c) || (extended && (c == 0x017F || c == 0x212A));
}

class Matcher {
 public:
  Matcher(const Program& p, const char16_t* in, int32_t len, const Limits& lim)
      : p_(p), in_(in), len_(len), unicode_(p.flags.unicode), lim_(lim) {
    size_t bytes = size_t(p.registerCount) * sizeof(int32_t);
    if (lim_.reserve) lim_.reserve(bytes);
    regs_.assign(size_t(p.registerCount), -1);
  }
  bool attempt(int32_t start);
  void addSteps(uint64_t n) { steps_ += n; }
  void flush() {
    if (!steps_) return;
    uint64_t n = steps_;
    steps_ = 0;
    if (lim_.charge) lim_.charge(n);
  }
  int32_t end() const { return end_; }
  int32_t reg(size_t i) const { return regs_[i]; }

 private:
  // the character at pos (forward) or before pos (backward) and its length; with u a surrogate pair is
  // one character, and no character can be read from inside a pair (V8 tries every code unit position
  // and its character matchers fail there)
  bool readFwd(int32_t pos, uint32_t& c, int32_t& w) const {
    if (pos >= len_) return false;
    c = in_[pos];
    w = 1;
    if (unicode_) {
      if (isLead(c)) {
        if (pos + 1 < len_ && isTrail(in_[pos + 1])) {
          c = combineSurrogates(c, in_[pos + 1]);
          w = 2;
        }
      } else if (isTrail(c) && pos > 0 && isLead(in_[pos - 1])) {
        return false;
      }
    }
    return true;
  }
  bool readBack(int32_t pos, uint32_t& c, int32_t& w) const {
    if (pos <= 0) return false;
    c = in_[pos - 1];
    w = 1;
    if (unicode_) {
      if (isTrail(c)) {
        if (pos >= 2 && isLead(in_[pos - 2])) {
          c = combineSurrogates(in_[pos - 2], c);
          w = 2;
        }
      } else if (isLead(c) && pos < len_ && isTrail(in_[pos])) {
        return false;
      }
    }
    return true;
  }
  bool insidePair(int32_t pos) const {
    return pos > 0 && pos < len_ && isTrail(in_[pos]) && isLead(in_[pos - 1]);
  }
  bool step(bool back, int32_t& pos, uint32_t& c) const {
    int32_t w;
    if (back ? !readBack(pos, c, w) : !readFwd(pos, c, w)) return false;
    pos += back ? -w : w;
    return true;
  }
  bool matchOne(int32_t op, int32_t arg, uint32_t c) const {
    switch (op) {
      case OP_CHAR: return c == uint32_t(arg);
      case OP_CLASS: return p_.classes[size_t(arg)].contains(c);
      case OP_ANY: return !isLineTerminator(c);
      default: return true;
    }
  }

  size_t stackBytes() const {
    return choices_.capacity() * sizeof(Choice) + undo_.capacity() * sizeof(Undo) + regs_.size() * sizeof(int32_t);
  }
  template <class T>
  void push(std::vector<T>& v, const T& x) {
    if (v.size() == v.capacity()) {
      size_t cap = v.capacity() < 64 ? 64 : v.capacity() * 2;
      if (lim_.reserve) lim_.reserve(stackBytes() + (cap - v.capacity()) * sizeof(T));
      v.reserve(cap);
    }
    v.push_back(x);
  }
  void choose(int32_t pc, int32_t pos, int32_t aux = 0) {
    push(choices_, Choice{pc, pos, aux, uint32_t(undo_.size())});
  }
  void set(int32_t r, int32_t v) {
    if (regs_[size_t(r)] == v) return;
    push(undo_, Undo{r, regs_[size_t(r)]});
    regs_[size_t(r)] = v;
  }
  void undoTo(size_t h) {
    while (undo_.size() > h) {
      const Undo& u = undo_.back();
      regs_[size_t(u.reg)] = u.old;
      undo_.pop_back();
    }
  }
  int32_t startReg(int32_t group) const { return int32_t(2 * (p_.captureCount + 1)) + group; }
  bool backtrack(int32_t& pc, int32_t& pos);
  bool star(const Inst& op, int32_t pc, int32_t& pos);
  bool backref(const Inst& op, int32_t& pos);

  const Program& p_;
  const char16_t* in_;
  int32_t len_;
  bool unicode_;
  const Limits& lim_;
  uint64_t steps_ = 0;
  int32_t end_ = 0;
  std::vector<int32_t> regs_;
  std::vector<Choice> choices_;
  std::vector<Undo> undo_;
};

bool Matcher::attempt(int32_t start) {
  const Inst* code = p_.code.data();
  int32_t pc = 0, pos = start;
  for (;;) {
    if (++steps_ >= kChargeBatch) flush();
    const Inst& op = code[pc];
    bool back = op.flags & kBackward;
    uint32_t c;
    // each case continues on success and breaks on failure
    switch (op.op) {
      case OP_CHAR:
        if (!step(back, pos, c) || c != uint32_t(op.a)) break;
        pc++;
        continue;
      case OP_STRING: {
        int32_t at = back ? pos - op.b : pos;
        if (at < 0 || at > len_ - op.b ||
            std::memcmp(in_ + at, p_.strings.data() + op.a, size_t(op.b) * sizeof(char16_t)) != 0)
          break;
        pos = back ? at : pos + op.b;
        steps_ += uint64_t(op.b);
        pc++;
        continue;
      }
      case OP_CLASS:
        if (!step(back, pos, c) || !p_.classes[size_t(op.a)].contains(c)) break;
        pc++;
        continue;
      case OP_ANY:
        if (!step(back, pos, c) || isLineTerminator(c)) break;
        pc++;
        continue;
      case OP_ANY_ALL:
        if (!step(back, pos, c)) break;
        pc++;
        continue;
      case OP_FAIL:
        break;
      case OP_BOL:
        if (pos != 0) break;
        pc++;
        continue;
      case OP_BOL_M:
        if (pos != 0 && !isLineTerminator(in_[pos - 1])) break;
        pc++;
        continue;
      case OP_EOL:
        if (pos != len_) break;
        pc++;
        continue;
      case OP_EOL_M:
        if (pos != len_ && !isLineTerminator(in_[pos])) break;
        pc++;
        continue;
      case OP_WORD_BOUNDARY:
      case OP_NOT_WORD_BOUNDARY: {
        bool ext = op.flags & kExtendedWord;
        bool a = pos > 0 && isWordChar(in_[pos - 1], ext);
        bool b = pos < len_ && isWordChar(in_[pos], ext);
        if ((a != b) != (op.op == OP_WORD_BOUNDARY)) break;
        pc++;
        continue;
      }
      case OP_SPLIT_NEXT:
        choose(op.a, pos);
        pc++;
        continue;
      case OP_SPLIT_JUMP:
        choose(pc + 1, pos);
        pc = op.a;
        continue;
      case OP_JMP:
        pc = op.a;
        continue;
      case OP_GROUP_OPEN:
        set(startReg(op.a), pos);
        pc++;
        continue;
      case OP_GROUP_CLOSE: {
        // a group read backwards (lookbehind) starts where its body ended
        int32_t s = regs_[size_t(startReg(op.a))];
        set(2 * op.a, std::min(s, pos));
        set(2 * op.a + 1, std::max(s, pos));
        pc++;
        continue;
      }
      case OP_RESET:
        for (int32_t g = op.a; g <= op.b; g++) {
          set(2 * g, -1);
          set(2 * g + 1, -1);
        }
        steps_ += uint64_t(op.b - op.a);
        pc++;
        continue;
      case OP_SAVE_POS:
        set(op.a, pos);
        pc++;
        continue;
      case OP_CHECK_PROGRESS:
        if (pos == regs_[size_t(op.a)]) break;
        pc++;
        continue;
      case OP_LOOP_INIT:
        set(op.a, 0);
        pc++;
        continue;
      case OP_LOOP: {
        int32_t k = regs_[size_t(op.a)];
        if (k >= op.c) {
          pc = op.d;
        } else if (k < op.b) {
          pc++;
        } else if (op.flags & kGreedy) {
          choose(op.d, pos);
          pc++;
        } else {
          choose(pc + 1, pos);
          pc = op.d;
        }
        continue;
      }
      case OP_LOOP_END: {
        int32_t k = regs_[size_t(op.a)];
        if (op.b >= 0 && k >= op.c && pos == regs_[size_t(op.b)]) break;
        if (k >= kInfinity - 1) break;  // unreachable: every iteration beyond the minimum consumes input
        set(op.a, k + 1);
        pc = op.d;
        continue;
      }
      case OP_BACKREF:
        // with u, V8 also rejects a reference that ends inside a surrogate pair
        if (!backref(op, pos) || (unicode_ && insidePair(pos))) break;
        pc++;
        continue;
      case OP_LOOK: {
        int32_t h = int32_t(choices_.size());
        choose(op.flags & kNegative ? op.b : kBarrier, pos);
        set(op.a, h);
        pc++;
        continue;
      }
      case OP_LOOK_END: {
        size_t h = size_t(regs_[size_t(op.a)]);
        if (!(op.flags & kNegative)) {
          // the body matched: back to where the lookaround started, without its choice points
          pos = choices_[h].pos;
          choices_.resize(h);
          pc++;
          continue;
        }
        // the body of a negative lookaround matched: the lookaround fails
        uint32_t undo = choices_[h].undo;
        choices_.resize(h);
        undoTo(undo);
        break;
      }
      case OP_STAR:
        if (!star(op, pc, pos)) break;
        pc++;
        continue;
      case OP_MATCH:
        end_ = pos;
        return true;
      default:
        break;
    }
    if (!backtrack(pc, pos)) return false;
  }
}

bool Matcher::backtrack(int32_t& pc, int32_t& pos) {
  for (;;) {
    if (choices_.empty()) {
      undoTo(0);  // the registers are back to their initial values for the next attempt
      return false;
    }
    if (++steps_ >= kChargeBatch) flush();
    Choice& ch = choices_.back();
    undoTo(ch.undo);
    if (ch.pc >= 0) {
      pc = ch.pc;
      pos = ch.pos;
      choices_.pop_back();
      return true;
    }
    if (ch.pc == kBarrier) {
      choices_.pop_back();
      continue;
    }
    int32_t sp = -2 - ch.pc;
    const Inst& st = p_.code[size_t(sp)];
    bool back = st.flags & kBackward;
    int32_t p = ch.pos;
    if (st.flags & kGreedy) {
      // give back one character (with u, a whole surrogate pair: pairs read the same in both directions)
      int32_t limit = ch.aux;
      if (!back) p -= unicode_ && p - 2 >= limit && isTrail(in_[p - 1]) && isLead(in_[p - 2]) ? 2 : 1;
      else p += unicode_ && p + 2 <= limit && isLead(in_[p]) && isTrail(in_[p + 1]) ? 2 : 1;
      if (p == limit) choices_.pop_back();
      else ch.pos = p;
    } else {
      // take one more character
      uint32_t c;
      int32_t count = ch.aux + 1;
      if (!step(back, p, c) || !matchOne(st.a, st.b, c)) {
        choices_.pop_back();
        continue;
      }
      if (count >= st.d) {
        choices_.pop_back();
      } else {
        ch.pos = p;
        ch.aux = count;
      }
    }
    pc = sp + 1;
    pos = p;
    return true;
  }
}

// A quantified single character: greedy takes as many as possible and leaves one choice point that
// gives them back one by one; lazy takes the minimum and leaves one that takes one more.
bool Matcher::star(const Inst& op, int32_t pc, int32_t& pos) {
  bool back = op.flags & kBackward;
  int32_t p = pos, count = 0;
  uint32_t c;
  if (op.flags & kGreedy) {
    int32_t minPos = op.c == 0 ? pos : -1;
    while (count < op.d) {
      int32_t q = p;
      if (!step(back, q, c) || !matchOne(op.a, op.b, c)) break;
      p = q;
      if (++count == op.c) minPos = p;
    }
    steps_ += uint64_t(count);
    if (count < op.c) return false;
    if (count > op.c) choose(starChoice(pc), p, minPos);
  } else {
    while (count < op.c) {
      if (!step(back, p, c) || !matchOne(op.a, op.b, c)) return false;
      count++;
    }
    steps_ += uint64_t(count);
    if (op.c < op.d) choose(starChoice(pc), p, op.c);
  }
  pos = p;
  return true;
}

// ES2024 BackreferenceMatcher: an undefined group matches empty; with i the characters compare after
// Canonicalize (with u, code point by code point).
bool Matcher::backref(const Inst& op, int32_t& pos) {
  int32_t s = regs_[size_t(2 * op.a)], e = regs_[size_t(2 * op.a + 1)];
  if (s < 0 || e < 0) return true;
  int32_t n = e - s;
  bool back = op.flags & kBackward;
  steps_ += uint64_t(n);
  if (!(op.flags & kIgnoreCase) || !unicode_) {
    int32_t at = back ? pos - n : pos;
    if (at < 0 || at > len_ - n) return false;
    if (!(op.flags & kIgnoreCase)) {
      if (std::memcmp(in_ + s, in_ + at, size_t(n) * sizeof(char16_t)) != 0) return false;
    } else {
      for (int32_t i = 0; i < n; i++)
        if (in_[s + i] != in_[at + i] && ucd::canonicalize(in_[s + i], false) != ucd::canonicalize(in_[at + i], false))
          return false;
    }
    pos = back ? at : pos + n;
    return true;
  }
  uint32_t a, b;
  int32_t wa, wb;
  if (!back) {
    for (int32_t i = s; i < e; i += wa) {
      // the captured text reads like the input (its ends are character boundaries)
      if (!readFwd(i, a, wa) || !readFwd(pos, b, wb)) return false;
      if (a != b && ucd::canonicalize(a, true) != ucd::canonicalize(b, true)) return false;
      pos += wb;
    }
  } else {
    for (int32_t i = e; i > s; i -= wa) {
      if (!readBack(i, a, wa) || !readBack(pos, b, wb)) return false;
      if (a != b && ucd::canonicalize(a, true) != ucd::canonicalize(b, true)) return false;
      pos -= wb;
    }
  }
  return true;
}

std::string sourceUtf8(const std::u16string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    uint32_t c = s[i];
    if (isLead(c) && i + 1 < s.size() && isTrail(s[i + 1])) c = combineSurrogates(c, s[++i]);
    else if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xF0 | (c >> 18));
      out += char(0x80 | ((c >> 12) & 0x3F));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  return out;
}

}  // namespace

void destroy(Program* p) { delete p; }

ProgramPtr compile(const std::u16string& source, const std::string& flags, std::string& err, const Limits* lim) {
  err.clear();
  CompileWork work;
  work.lim = lim;
  Flags f;
  for (char c : flags) {
    bool* slot = c == 'd' ? &f.hasIndices : c == 'g' ? &f.global : c == 'i' ? &f.ignoreCase
               : c == 'm' ? &f.multiline : c == 's' ? &f.dotAll : c == 'u' ? &f.unicode
               : c == 'y' ? &f.sticky : nullptr;
    if (!slot || *slot) {
      err = "Invalid flags supplied to RegExp constructor '" + flags + "'";
      return nullptr;
    }
    *slot = true;
  }
  std::string text;  // V8 prints the flags in this order
  for (char c : std::string("dgimsuy"))
    if (flags.find(c) != std::string::npos) text += c;
  Ast ast;
  std::string msg;
  bool ok = parse(source, f, ast, msg, work);
  ProgramPtr p(new Program);
  if (ok) {
    p->flags = f;
    p->flagText = text;
    p->captureCount = ast.captureCount;
    p->names = ast.names;
    if (!generate(ast, *p, work)) {
      ok = false;
      msg = "Regular expression too large";
    }
  }
  work.flush();
  if (!ok) {
    err = "Invalid regular expression: /" + sourceUtf8(source) + "/" + text + ": " + msg;
    return nullptr;
  }
  return p;
}

uint32_t captureCount(const Program& p) { return p.captureCount; }
const std::vector<NamedGroup>& namedGroups(const Program& p) { return p.names; }
bool hasFlag(const Program& p, char f) { return f != 0 && p.flagText.find(f) != std::string::npos; }
const std::string& flagText(const Program& p) { return p.flagText; }
uint32_t registerCount(const Program& p) { return uint32_t(p.registerCount); }
size_t programSize(const Program& p) {
  size_t n = sizeof(Program) + p.code.size() * sizeof(Inst) + p.strings.size() * sizeof(char16_t) +
             p.first.ranges.size() * sizeof(Range);
  for (const CharClass& c : p.classes) n += sizeof(CharClass) + c.ranges.size() * sizeof(Range);
  for (const NamedGroup& g : p.names) n += sizeof(NamedGroup) + g.name.size();
  return n;
}
const char* unicodeVersion() { return ucd::version(); }

bool exec(const Program& p, const char16_t* input, size_t len, size_t start, bool sticky,
          std::vector<int32_t>& caps, const Limits& lim) {
  if (len >= size_t(INT32_MAX) || start > len) return false;
  const int32_t n = int32_t(len);
  int32_t s = int32_t(start);
  // With u, V8 starts a match inside a surrogate pair at its lead surrogate first (and a sticky match
  // then also at the given index); the search then advances one code unit at a time, like without u,
  // since no character can be read from inside a pair (but empty matches can happen there).
  bool retryInside = false;
  if (p.flags.unicode && s > 0 && s < n && isTrail(input[s]) && isLead(input[s - 1])) {
    s--;
    retryInside = sticky;
  }
  Matcher m(p, input, n, lim);
  m.addSteps(uint64_t(p.registerCount) / 8 + 1);  // setting up the registers and the captures
  bool found = false;
  int32_t at = s;
  if (p.anchored) {
    found = s == 0 && m.attempt(0);
  } else if (sticky) {
    found = m.attempt(s) || (retryInside && m.attempt(at = s + 1));
  } else {
    for (;; at++) {
      if (p.hasFirst) {
        // skip to a code unit that can start a match (the pattern cannot match the empty string)
        int32_t q = at;
        if (p.firstUnit >= 0) {
          const char16_t* f = std::char_traits<char16_t>::find(input + q, size_t(n - q), char16_t(p.firstUnit));
          q = f ? int32_t(f - input) : n;
        } else {
          while (q < n && !p.first.contains(input[q])) q++;
        }
        m.addSteps(uint64_t(q - at));
        if (q >= n) break;
        at = q;
      }
      if (m.attempt(at)) {
        found = true;
        break;
      }
      if (at >= n) break;
    }
  }
  m.flush();
  if (!found) return false;
  caps.assign(2 * (size_t(p.captureCount) + 1), -1);
  caps[0] = at;
  caps[1] = m.end();
  for (size_t i = 2; i < caps.size(); i++) caps[i] = m.reg(i);
  return true;
}

}  // namespace e7::re
