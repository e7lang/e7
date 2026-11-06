// Ast to program. Each node becomes a few instructions (no node is duplicated, so the program grows
// linearly with the pattern); quantifiers use counters and registers instead of unrolling, and a
// quantified single character becomes one OP_STAR. Lookbehind bodies are generated right to left.
#include <algorithm>

#include "regexp/regexp_impl.h"

namespace e7::re {
namespace {

constexpr size_t kMaxProgramSize = size_t(1) << 24;

class Generator {
 public:
  Generator(const Ast& ast, Program& p, CompileWork& work)
      : ast_(ast), p_(p), work_(work), classIndex_(ast.classes.size(), -1) {}
  bool run();

 private:
  struct Info {
    bool canBeEmpty = false;  // can match the empty string
    bool onlyEmpty = false;   // can only match the empty string (V8's max_match == 0)
    uint32_t capLo = UINT32_MAX, capHi = 0;  // the capture groups inside
  };
  int32_t pc() const { return int32_t(p_.code.size()); }
  int32_t emit(uint8_t op, int32_t a = 0, int32_t b = 0, int32_t c = 0, int32_t d = 0, uint8_t flags = 0) {
    work_.charge(1);
    work_.grow(2 * sizeof(Inst));
    Inst in;
    in.op = op;
    in.flags = flags;
    in.a = a, in.b = b, in.c = c, in.d = d;
    p_.code.push_back(in);
    return pc() - 1;
  }
  int32_t newRegister() { return p_.registerCount++; }
  int32_t classFor(uint32_t astClass);
  void analyze();
  void gen(int32_t id, bool back);
  void genSeq(const Node& n, bool back);
  void genRepeat(const Node& n, bool back);
  bool singleChar(int32_t id, int32_t& op, int32_t& arg);
  bool firstSet(int32_t id, Ranges& acc, bool& any) const;
  bool anchored(int32_t id) const;
  void setupSearch();
  Ranges dotRanges() const;

  const Ast& ast_;
  Program& p_;
  CompileWork& work_;
  std::vector<int32_t> classIndex_;
  std::vector<Info> info_;
};

int32_t Generator::classFor(uint32_t astClass) {
  if (classIndex_[astClass] < 0) {
    classIndex_[astClass] = int32_t(p_.classes.size());
    work_.charge(ast_.classes[astClass].size() + 8);
    work_.grow(sizeof(CharClass) + ast_.classes[astClass].size() * sizeof(Range));
    p_.classes.emplace_back(ast_.classes[astClass]);
  }
  return classIndex_[astClass];
}

// Children are created before their parents, so one pass in index order sees every child first.
void Generator::analyze() {
  work_.charge(ast_.nodes.size());
  work_.grow(ast_.nodes.size() * sizeof(Info));
  info_.resize(ast_.nodes.size());
  for (size_t i = 0; i < ast_.nodes.size(); i++) {
    const Node& n = ast_.nodes[i];
    Info& f = info_[i];
    auto takeCaps = [&f](const Info& k) {
      f.capLo = std::min(f.capLo, k.capLo);
      f.capHi = std::max(f.capHi, k.capHi);
    };
    switch (n.kind) {
      case NodeKind::Char:
      case NodeKind::Class:
      case NodeKind::Dot:
        break;
      case NodeKind::Backref:
        f.canBeEmpty = true;
        break;
      case NodeKind::Seq:
      case NodeKind::Alt: {
        bool seq = n.kind == NodeKind::Seq;
        f.canBeEmpty = seq;
        f.onlyEmpty = true;
        for (int32_t k : n.kids) {
          const Info& ki = info_[k];
          f.canBeEmpty = seq ? f.canBeEmpty && ki.canBeEmpty : f.canBeEmpty || ki.canBeEmpty;
          f.onlyEmpty = f.onlyEmpty && ki.onlyEmpty;
          takeCaps(ki);
        }
        break;
      }
      case NodeKind::Capture:
      case NodeKind::Group:
        f = info_[n.child];
        if (n.kind == NodeKind::Capture) {
          f.capLo = std::min(f.capLo, n.value);
          f.capHi = std::max(f.capHi, n.value);
        }
        break;
      case NodeKind::Look:
        f.canBeEmpty = f.onlyEmpty = true;
        takeCaps(info_[n.child]);
        break;
      case NodeKind::Repeat: {
        const Info& c = info_[n.child];
        f.canBeEmpty = n.min == 0 || c.canBeEmpty;
        f.onlyEmpty = n.max == 0 || c.onlyEmpty;
        takeCaps(c);
        break;
      }
      default:  // Empty and the assertions
        f.canBeEmpty = f.onlyEmpty = true;
        break;
    }
  }
}

Ranges Generator::dotRanges() const {
  uint32_t maxChar = ast_.flags.unicode ? kMaxCodePoint : 0xFFFF;
  if (ast_.flags.dotAll) return {{0, maxChar}};
  return complement({{0x0A, 0x0A}, {0x0D, 0x0D}, {0x2028, 0x2029}}, maxChar);
}

void Generator::gen(int32_t id, bool back) {
  const Node& n = ast_.nodes[id];
  const Flags& fl = ast_.flags;
  uint8_t dir = back ? kBackward : 0;
  switch (n.kind) {
    case NodeKind::Empty:
      break;
    case NodeKind::Char:
      emit(OP_CHAR, int32_t(n.value), 0, 0, 0, dir);
      break;
    case NodeKind::Class: {
      const Ranges& r = ast_.classes[n.value];
      if (r.size() == 1 && r[0].lo == r[0].hi) emit(OP_CHAR, int32_t(r[0].lo), 0, 0, 0, dir);
      else emit(OP_CLASS, classFor(n.value), 0, 0, 0, dir);
      break;
    }
    case NodeKind::Dot:
      emit(fl.dotAll ? OP_ANY_ALL : OP_ANY, 0, 0, 0, 0, dir);
      break;
    case NodeKind::Seq:
      genSeq(n, back);
      break;
    case NodeKind::Alt: {
      std::vector<int32_t> jumps;
      for (size_t i = 0; i + 1 < n.kids.size(); i++) {
        int32_t split = emit(OP_SPLIT_NEXT);
        gen(n.kids[i], back);
        jumps.push_back(emit(OP_JMP));
        p_.code[split].a = pc();
      }
      gen(n.kids.back(), back);
      for (int32_t j : jumps) p_.code[j].a = pc();
      break;
    }
    case NodeKind::Capture:
      emit(OP_GROUP_OPEN, int32_t(n.value));
      gen(n.child, back);
      emit(OP_GROUP_CLOSE, int32_t(n.value));
      break;
    case NodeKind::Group:
      gen(n.child, back);
      break;
    case NodeKind::Backref:
      emit(OP_BACKREF, int32_t(n.value), 0, 0, 0, uint8_t(dir | (fl.ignoreCase ? kIgnoreCase : 0)));
      break;
    case NodeKind::Bol:
      emit(fl.multiline ? OP_BOL_M : OP_BOL);
      break;
    case NodeKind::Eol:
      emit(fl.multiline ? OP_EOL_M : OP_EOL);
      break;
    case NodeKind::WordBoundary:
    case NodeKind::NotWordBoundary:
      emit(n.kind == NodeKind::WordBoundary ? OP_WORD_BOUNDARY : OP_NOT_WORD_BOUNDARY, 0, 0, 0, 0,
           fl.unicode && fl.ignoreCase ? kExtendedWord : 0);
      break;
    case NodeKind::Look: {
      int32_t reg = newRegister();
      uint8_t neg = n.negative ? kNegative : 0;
      int32_t look = emit(OP_LOOK, reg, 0, 0, 0, neg);
      gen(n.child, n.behind);
      emit(OP_LOOK_END, reg, 0, 0, 0, neg);
      p_.code[look].b = pc();
      break;
    }
    case NodeKind::Repeat:
      genRepeat(n, back);
      break;
  }
}

// Runs of literal characters become one OP_STRING (left to right, also when reading backwards). With
// u, lone surrogates stay single characters: they must not match half of a pair.
void Generator::genSeq(const Node& n, bool back) {
  auto literal = [this](int32_t id) {
    const Node& k = ast_.nodes[id];
    return k.kind == NodeKind::Char && !(ast_.flags.unicode && k.value >= 0xD800 && k.value <= 0xDFFF);
  };
  std::vector<std::pair<size_t, size_t>> segments;  // [first, last) kid indices
  for (size_t i = 0; i < n.kids.size();) {
    size_t j = i + 1;
    if (literal(n.kids[i]))
      while (j < n.kids.size() && literal(n.kids[j])) j++;
    segments.push_back({i, j});
    i = j;
  }
  for (size_t s = 0; s < segments.size(); s++) {
    auto [first, last] = segments[back ? segments.size() - 1 - s : s];
    if (last - first == 1) {
      gen(n.kids[first], back);
      continue;
    }
    int32_t offset = int32_t(p_.strings.size());
    for (size_t k = first; k < last; k++) {
      uint32_t c = ast_.nodes[n.kids[k]].value;
      if (c > 0xFFFF) {
        p_.strings += char16_t(0xD800 + ((c - 0x10000) >> 10));
        p_.strings += char16_t(0xDC00 + ((c - 0x10000) & 0x3FF));
      } else {
        p_.strings += char16_t(c);
      }
    }
    emit(OP_STRING, offset, int32_t(p_.strings.size()) - offset, 0, 0, back ? kBackward : 0);
  }
}

bool Generator::singleChar(int32_t id, int32_t& op, int32_t& arg) {
  const Node& n = ast_.nodes[id];
  switch (n.kind) {
    case NodeKind::Char:
      op = OP_CHAR, arg = int32_t(n.value);
      return true;
    case NodeKind::Class:
      op = OP_CLASS, arg = classFor(n.value);
      return true;
    case NodeKind::Dot:
      op = ast_.flags.dotAll ? OP_ANY_ALL : OP_ANY, arg = 0;
      return true;
    case NodeKind::Group:
      return singleChar(n.child, op, arg);
    default:
      return false;
  }
}

// ES2024 RepeatMatcher: each iteration resets the captures inside the atom, and an iteration beyond
// the minimum that matches the empty string fails (the empty check).
void Generator::genRepeat(const Node& n, bool back) {
  const Info& ci = info_[n.child];
  if (n.max == 0) return;
  // An atom that can only match empty: dropped when optional, else matched once (as V8; the result
  // is the same as iterating)
  if (ci.onlyEmpty) {
    if (n.min > 0) gen(n.child, back);
    return;
  }
  if (n.min == 1 && n.max == 1) {
    gen(n.child, back);
    return;
  }
  uint8_t dir = back ? kBackward : 0;
  uint8_t greedy = n.greedy ? kGreedy : 0;
  int32_t op, arg;
  if (singleChar(n.child, op, arg)) {
    emit(OP_STAR, op, arg, n.min, n.max, uint8_t(dir | greedy));
    return;
  }
  bool hasCaps = ci.capLo <= ci.capHi;
  auto prologue = [&](int32_t posReg) {
    if (posReg >= 0) emit(OP_SAVE_POS, posReg);
    if (hasCaps) emit(OP_RESET, int32_t(ci.capLo), int32_t(ci.capHi));
  };
  if (n.min == 0 && (n.max == 1 || n.max == kInfinity)) {
    // x? and x*: a split before the body; x* jumps back to it
    int32_t posReg = ci.canBeEmpty ? newRegister() : -1;
    int32_t split = emit(n.greedy ? OP_SPLIT_NEXT : OP_SPLIT_JUMP);
    prologue(posReg);
    gen(n.child, back);
    if (posReg >= 0) emit(OP_CHECK_PROGRESS, posReg);
    if (n.max == kInfinity) emit(OP_JMP, split);
    p_.code[split].a = pc();
    return;
  }
  if (n.min == 1 && n.max == kInfinity && !ci.canBeEmpty) {
    // x+ where x always consumes: the body, then a split back to it
    int32_t body = pc();
    prologue(-1);
    gen(n.child, back);
    emit(n.greedy ? OP_SPLIT_JUMP : OP_SPLIT_NEXT, body);
    return;
  }
  int32_t counter = newRegister();
  int32_t posReg = ci.canBeEmpty ? newRegister() : -1;
  emit(OP_LOOP_INIT, counter);
  int32_t head = emit(OP_LOOP, counter, n.min, n.max, 0, greedy);
  prologue(posReg);
  gen(n.child, back);
  emit(OP_LOOP_END, counter, posReg, n.min, head);
  p_.code[head].d = pc();
}

// The code points a match can start with; false when the node can match the empty string. `any` is set
// when it cannot be known (a backreference).
bool Generator::firstSet(int32_t id, Ranges& acc, bool& any) const {
  const Node& n = ast_.nodes[id];
  switch (n.kind) {
    case NodeKind::Char:
      acc.push_back({n.value, n.value});
      return false;
    case NodeKind::Class: {
      const Ranges& r = ast_.classes[n.value];
      work_.charge(r.size() + 1);
      work_.grow(r.size() * sizeof(Range));
      acc.insert(acc.end(), r.begin(), r.end());
      return false;
    }
    case NodeKind::Dot: {
      Ranges r = dotRanges();
      acc.insert(acc.end(), r.begin(), r.end());
      return false;
    }
    case NodeKind::Backref:
      any = true;
      return true;
    case NodeKind::Seq:
      for (int32_t k : n.kids)
        if (!firstSet(k, acc, any)) return false;
      return true;
    case NodeKind::Alt: {
      bool empty = false;
      for (int32_t k : n.kids) empty = firstSet(k, acc, any) || empty;
      return empty;
    }
    case NodeKind::Capture:
    case NodeKind::Group:
      return firstSet(n.child, acc, any);
    case NodeKind::Repeat:
      if (n.max == 0 || info_[n.child].onlyEmpty) return true;
      return firstSet(n.child, acc, any) || n.min == 0;
    default:  // empty, assertions and lookarounds (zero width; ignoring them only widens the set)
      return true;
  }
}

bool Generator::anchored(int32_t id) const {
  const Node& n = ast_.nodes[id];
  switch (n.kind) {
    case NodeKind::Bol:
      return !ast_.flags.multiline;
    case NodeKind::Seq:
      return anchored(n.kids[0]);
    case NodeKind::Alt:
      for (int32_t k : n.kids)
        if (!anchored(k)) return false;
      return true;
    case NodeKind::Capture:
    case NodeKind::Group:
      return anchored(n.child);
    default:
      return false;
  }
}

void Generator::setupSearch() {
  p_.anchored = anchored(ast_.root);
  Ranges first;
  bool any = false;
  if (firstSet(ast_.root, first, any) || any) return;
  work_.charge(first.size() * 8);
  // as code units: an astral code point starts with its lead surrogate
  Ranges units;
  for (const Range& r : first) {
    if (r.lo <= 0xFFFF) units.push_back({r.lo, std::min<uint32_t>(r.hi, 0xFFFF)});
    if (r.hi > 0xFFFF) {
      uint32_t lo = std::max<uint32_t>(r.lo, 0x10000);
      units.push_back({0xD800 + ((lo - 0x10000) >> 10), 0xD800 + ((r.hi - 0x10000) >> 10)});
    }
  }
  normalize(units);
  if (units.size() == 1 && units[0].lo == units[0].hi) p_.firstUnit = int32_t(units[0].lo);
  p_.hasFirst = true;
  p_.first = CharClass(units);
}

bool Generator::run() {
  // registers: 2 per capture (group 0 included), then the start register of each group
  p_.registerCount = int32_t(3 * (ast_.captureCount + 1));
  analyze();
  gen(ast_.root, false);
  emit(OP_MATCH);
  if (p_.code.size() > kMaxProgramSize) return false;
  setupSearch();
  return true;
}

}  // namespace

bool generate(const Ast& ast, Program& p, CompileWork& work) {
  Generator g(ast, p, work);
  return g.run();
}

}  // namespace e7::re
