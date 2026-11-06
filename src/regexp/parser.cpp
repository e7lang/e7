// Pattern parser: source text to Ast. It follows the structure of V8's RegExpParser so that the same
// patterns are accepted (the Annex B grammar without the u flag), the same error is reported first and
// with the same message. Groups are parsed with an explicit stack of states, not recursion.
#include <utility>

#include "regexp/regexp_impl.h"

namespace e7::re {
namespace {

constexpr uint32_t kEnd = 1u << 21;  // past the end of the pattern (not a code point)

const char kNothingToRepeat[] = "Nothing to repeat";
const char kInvalidEscape[] = "Invalid escape";
const char kInvalidUnicodeEscape[] = "Invalid Unicode escape";
const char kInvalidDecimalEscape[] = "Invalid decimal escape";
const char kInvalidGroupName[] = "Invalid capture group name";
const char kLoneQuantifierBrackets[] = "Lone quantifier brackets";

bool isDigit(uint32_t c) { return c >= '0' && c <= '9'; }
int hexValue(uint32_t c) {
  if (c >= '0' && c <= '9') return int(c - '0');
  if (c >= 'a' && c <= 'f') return int(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return int(c - 'A' + 10);
  return -1;
}
bool isSyntaxChar(uint32_t c) {
  switch (c) {
    case '^': case '$': case '\\': case '.': case '*': case '+': case '?':
    case '(': case ')': case '[': case ']': case '{': case '}': case '|':
      return true;
    default:
      return false;
  }
}
bool isPropertyNameChar(uint32_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDigit(c) || c == '_';
}

using Name = std::vector<uint32_t>;  // a group name in code points

std::string toUtf8(const Name& name) {
  std::string s;
  for (uint32_t c : name) {
    if (c < 0x80) {
      s += char(c);
    } else if (c < 0x800) {
      s += char(0xC0 | (c >> 6));
      s += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      s += char(0xE0 | (c >> 12));
      s += char(0x80 | ((c >> 6) & 0x3F));
      s += char(0x80 | (c & 0x3F));
    } else {
      s += char(0xF0 | (c >> 18));
      s += char(0x80 | ((c >> 12) & 0x3F));
      s += char(0x80 | ((c >> 6) & 0x3F));
      s += char(0x80 | (c & 0x3F));
    }
  }
  return s;
}

class Parser {
 public:
  Parser(const std::u16string& src, const Flags& flags, Ast& ast, std::string& err)
      : src_(src), flags_(flags), ast_(ast), err_(err) {
    maxChar_ = flags.unicode ? kMaxCodePoint : 0xFFFF;
  }
  bool run();

 private:
  enum class Type : uint8_t { Top, Capture, Group, Look };
  struct State {
    Type type = Type::Top;
    bool negative = false, behind = false;
    uint32_t capture = 0;
    Name name;
    std::vector<int32_t> alternatives, terms;
  };

  // ---- reading: cur_ is the character at curPos_; with the u flag (or inside a group name) a surrogate
  // pair is one character
  bool unicodeMode() const { return flags_.unicode || forceUnicode_; }
  uint32_t readAt(size_t i, size_t* next) const {
    uint32_t c = src_[i++];
    if (unicodeMode() && isLead(c) && i < src_.size() && isTrail(src_[i])) c = combineSurrogates(c, src_[i++]);
    *next = i;
    return c;
  }
  void advance() {
    curPos_ = nextPos_;
    if (nextPos_ >= src_.size()) {
      curPos_ = src_.size();
      cur_ = kEnd;
      return;
    }
    cur_ = readAt(nextPos_, &nextPos_);
  }
  void advance(int n) {
    while (n-- > 0) advance();
  }
  void reset(size_t pos) {
    nextPos_ = pos;
    advance();
  }
  uint32_t next() const {
    size_t ignored;
    return nextPos_ < src_.size() ? readAt(nextPos_, &ignored) : kEnd;
  }
  bool fail(const char* msg) {
    if (err_.empty()) err_ = msg;
    return false;
  }

  // ---- building
  int32_t add(const Node& n) {
    ast_.nodes.push_back(n);
    return int32_t(ast_.nodes.size() - 1);
  }
  int32_t add(NodeKind k, uint32_t value = 0) {
    Node n;
    n.kind = k;
    n.value = value;
    return add(n);
  }
  void addTerm(int32_t n) { states_.back().terms.push_back(n); }
  int32_t classNode(Ranges r, bool negated);
  void addChar(uint32_t c);
  int32_t makeSeq(std::vector<int32_t>& terms);
  int32_t finishDisjunction(State& s);
  Ranges classEscape(uint32_t c) const;

  // ---- grammar
  bool parseQuantifier();
  bool parseInterval(int32_t& min, int32_t& max);
  bool openGroup();
  bool closeGroup();
  bool parseGroupName(Name& name);
  bool parseAtomEscape(bool& quantifiable);
  bool parseNamedBackref();
  bool parseBackrefIndex(uint32_t& index);
  bool parseCharacterEscape(bool inClass, uint32_t& out);
  bool parseUnicodeEscape(uint32_t& v);
  bool parseHex(int digits, uint32_t& v);
  bool parseUnlimitedHex(uint32_t& v);
  uint32_t parseOctal();
  bool parseClass(int32_t& out);
  bool parseClassAtom(Ranges& r, uint32_t& c, bool& isClass);
  bool parseProperty(bool negate, bool inClass, Ranges& out);
  void scanForCaptures(bool inClass);
  bool hasNamedCaptures(bool inClass);
  bool insideCapture(uint32_t index) const;
  bool insideCapture(const Name& name) const;

  const std::u16string& src_;
  const Flags& flags_;
  Ast& ast_;
  std::string& err_;
  uint32_t maxChar_;
  uint32_t cur_ = kEnd;
  size_t curPos_ = 0, nextPos_ = 0;
  bool forceUnicode_ = false;
  std::vector<State> states_;
  uint32_t capturesStarted_ = 0;
  uint32_t captureCount_ = 0;  // all groups of the pattern, once scanned
  bool scanned_ = false;
  bool hasNamed_ = false;
  std::vector<std::pair<Name, uint32_t>> names_;
  std::vector<std::pair<int32_t, Name>> namedRefs_;  // \k<name> nodes resolved at the end
};

int32_t Parser::classNode(Ranges r, bool negated) {
  normalize(r);
  if (flags_.ignoreCase) ucd::closeOverCase(r, flags_.unicode);
  if (negated) r = complement(r, maxChar_);
  ast_.classes.push_back(std::move(r));
  return add(NodeKind::Class, uint32_t(ast_.classes.size() - 1));
}

void Parser::addChar(uint32_t c) {
  if (flags_.ignoreCase) {
    Ranges r;
    ucd::caseEquivalents(c, flags_.unicode, r);
    if (r.size() > 1) {
      addTerm(classNode(std::move(r), false));
      return;
    }
  }
  addTerm(add(NodeKind::Char, c));
}

// the terms of one alternative as a node; terms is left empty
int32_t Parser::makeSeq(std::vector<int32_t>& terms) {
  if (terms.size() <= 1) {
    int32_t n = terms.empty() ? add(NodeKind::Empty) : terms[0];
    terms.clear();
    return n;
  }
  Node n;
  n.kind = NodeKind::Seq;
  n.kids = std::move(terms);
  terms.clear();
  return add(n);
}

int32_t Parser::finishDisjunction(State& s) {
  int32_t seq = makeSeq(s.terms);
  if (s.alternatives.empty()) return seq;
  s.alternatives.push_back(seq);
  Node n;
  n.kind = NodeKind::Alt;
  n.kids = std::move(s.alternatives);
  return add(n);
}

Ranges Parser::classEscape(uint32_t c) const {
  Ranges r;
  switch (c | 0x20) {
    case 'd':
      r = {{'0', '9'}};
      break;
    case 's':
      r = {{0x09, 0x0D}, {0x20, 0x20}, {0xA0, 0xA0}, {0x1680, 0x1680}, {0x2000, 0x200A}, {0x2028, 0x2029},
           {0x202F, 0x202F}, {0x205F, 0x205F}, {0x3000, 0x3000}, {0xFEFF, 0xFEFF}};
      break;
    default:  // w: with u and i, WordCharacters adds the characters whose canonical form is a word character
      r = {{'0', '9'}, {'A', 'Z'}, {'_', '_'}, {'a', 'z'}};
      if (flags_.unicode && flags_.ignoreCase) ucd::closeOverCase(r, true);
      break;
  }
  if (c == 'D' || c == 'S' || c == 'W') r = complement(r, maxChar_);
  return r;
}

bool Parser::run() {
  states_.emplace_back();
  advance();
  for (;;) {
    switch (cur_) {
      case kEnd: {
        if (states_.size() > 1) return fail("Unterminated group");
        ast_.root = finishDisjunction(states_.back());
        for (auto& [node, name] : namedRefs_) {
          bool found = false;
          for (auto& [n, index] : names_)
            if (n == name) {
              ast_.nodes[node].value = index;
              found = true;
            }
          if (!found) return fail("Invalid named capture referenced");
        }
        ast_.captureCount = capturesStarted_;
        for (auto& [name, index] : names_) ast_.names.push_back({toUtf8(name), index});  // source order
        return true;
      }
      case ')':
        if (!closeGroup()) return false;
        break;
      case '|': {
        advance();
        State& s = states_.back();
        s.alternatives.push_back(makeSeq(s.terms));
        continue;
      }
      case '*':
      case '+':
      case '?':
        return fail(kNothingToRepeat);
      case '^':
        advance();
        addTerm(add(NodeKind::Bol));
        continue;
      case '$':
        advance();
        addTerm(add(NodeKind::Eol));
        continue;
      case '.':
        advance();
        addTerm(add(NodeKind::Dot));
        break;
      case '(':
        if (!openGroup()) return false;
        continue;
      case '[': {
        int32_t n;
        if (!parseClass(n)) return false;
        addTerm(n);
        break;
      }
      case '\\': {
        bool quantifiable = true;
        if (!parseAtomEscape(quantifiable)) return false;
        if (!quantifiable) continue;
        break;
      }
      case '{': {
        int32_t min, max;
        if (parseInterval(min, max)) return fail(kNothingToRepeat);
        if (flags_.unicode) return fail(kLoneQuantifierBrackets);
        addChar('{');
        advance();
        break;
      }
      case '}':
      case ']':
        if (flags_.unicode) return fail(kLoneQuantifierBrackets);
        addChar(cur_);
        advance();
        break;
      default:
        addChar(cur_);
        advance();
        break;
    }
    if (!parseQuantifier()) return false;
  }
}

bool Parser::parseQuantifier() {
  int32_t min, max;
  switch (cur_) {
    case '*':
      min = 0, max = kInfinity;
      advance();
      break;
    case '+':
      min = 1, max = kInfinity;
      advance();
      break;
    case '?':
      min = 0, max = 1;
      advance();
      break;
    case '{':
      if (parseInterval(min, max)) {
        if (max < min) return fail("numbers out of order in {} quantifier");
        break;
      }
      if (flags_.unicode) return fail("Incomplete quantifier");
      return true;  // a literal '{', read by the next round
    default:
      return true;
  }
  bool greedy = true;
  if (cur_ == '?') {
    greedy = false;
    advance();
  }
  State& s = states_.back();
  int32_t atom = s.terms.back();
  s.terms.pop_back();
  const Node& a = ast_.nodes[atom];
  // Annex B allows quantified lookaheads without u; lookbehinds are never quantifiable
  if (a.kind == NodeKind::Look && (flags_.unicode || a.behind)) return fail("Invalid quantifier");
  Node n;
  n.kind = NodeKind::Repeat;
  n.child = atom;
  n.min = min;
  n.max = max;
  n.greedy = greedy;
  addTerm(add(n));
  return true;
}

// {n}, {n,}, {n,m}; numbers saturate at kInfinity as in V8. Without a valid quantifier the position is
// restored and false returned (no error).
bool Parser::parseInterval(int32_t& minOut, int32_t& maxOut) {
  size_t start = curPos_;
  auto number = [this]() {
    int32_t v = 0;
    while (isDigit(cur_)) {
      int32_t d = int32_t(cur_ - '0');
      if (v > (kInfinity - d) / 10) {
        while (isDigit(cur_)) advance();
        return kInfinity;
      }
      v = 10 * v + d;
      advance();
    }
    return v;
  };
  advance();
  if (!isDigit(cur_)) {
    reset(start);
    return false;
  }
  int32_t min = number(), max;
  if (cur_ == '}') {
    max = min;
    advance();
  } else if (cur_ == ',') {
    advance();
    if (cur_ == '}') {
      max = kInfinity;
      advance();
    } else {
      max = number();
      if (cur_ != '}') {
        reset(start);
        return false;
      }
      advance();
    }
  } else {
    reset(start);
    return false;
  }
  minOut = min;
  maxOut = max;
  return true;
}

bool Parser::openGroup() {
  advance();  // (
  State s;
  s.type = Type::Capture;
  bool named = false;
  if (cur_ == '?') {
    switch (next()) {
      case ':':
        advance(2);
        s.type = Type::Group;
        break;
      case '=':
      case '!':
        s.negative = next() == '!';
        advance(2);
        s.type = Type::Look;
        break;
      case '<':
        advance();
        if (next() == '=' || next() == '!') {
          s.negative = next() == '!';
          advance(2);
          s.type = Type::Look;
          s.behind = true;
          break;
        }
        named = true;
        hasNamed_ = true;
        advance();
        break;
      default:
        return fail("Invalid group");
    }
  }
  if (s.type == Type::Capture) {
    if (capturesStarted_ >= kMaxCaptures) return fail("Too many captures");
    s.capture = ++capturesStarted_;
    if (named) {
      if (!parseGroupName(s.name)) return false;
      for (auto& entry : names_)
        if (entry.first == s.name) return fail("Duplicate capture group name");
      names_.push_back({s.name, s.capture});
    }
  }
  if (states_.size() > size_t(kMaxNesting)) return fail("Maximum call stack size exceeded");
  states_.push_back(std::move(s));
  return true;
}

bool Parser::closeGroup() {
  if (states_.size() == 1) return fail("Unmatched ')'");
  advance();
  State& s = states_.back();
  Node n;
  n.child = finishDisjunction(s);
  switch (s.type) {
    case Type::Capture:
      n.kind = NodeKind::Capture;
      n.value = s.capture;
      break;
    case Type::Group:
      n.kind = NodeKind::Group;
      break;
    default:
      n.kind = NodeKind::Look;
      n.negative = s.negative;
      n.behind = s.behind;
      break;
  }
  states_.pop_back();
  addTerm(add(n));
  return true;
}

// RegExpIdentifierName: read with surrogate pairs combined and \u{...} allowed even without u (as V8's
// ForceUnicodeScope). cur_ is the first character of the name; on return cur_ is the one after '>'.
bool Parser::parseGroupName(Name& name) {
  forceUnicode_ = true;
  reset(curPos_);
  for (bool atStart = true;;) {
    uint32_t c = cur_;
    if (c == '\\' && next() == 'u') {
      advance(2);
      if (!parseUnicodeEscape(c)) return fail(kInvalidUnicodeEscape);
    } else {
      advance();
    }
    if (c == '\\') return fail(kInvalidGroupName);
    if (atStart) {
      if (!ucd::isIdStart(c)) return fail(kInvalidGroupName);
      atStart = false;
    } else {
      if (c == '>') break;  // also an escaped '>', as in V8
      if (!ucd::isIdContinue(c)) return fail(kInvalidGroupName);
    }
    name.push_back(c);
  }
  forceUnicode_ = false;
  reset(curPos_);
  return true;
}

bool Parser::insideCapture(uint32_t index) const {
  for (const State& s : states_)
    if (s.type == Type::Capture && s.capture == index) return true;
  return false;
}

bool Parser::insideCapture(const Name& name) const {
  for (const State& s : states_)
    if (s.type == Type::Capture && s.name == name) return true;
  return false;
}

// cur_ is the backslash of an escape outside a class
bool Parser::parseAtomEscape(bool& quantifiable) {
  uint32_t n = next();
  switch (n) {
    case kEnd:
      return fail("\\ at end of pattern");
    case 'b':
    case 'B':
      advance(2);
      addTerm(add(n == 'b' ? NodeKind::WordBoundary : NodeKind::NotWordBoundary));
      quantifiable = false;
      return true;
    case 'd': case 'D': case 's': case 'S': case 'w': case 'W':
      advance(2);
      addTerm(classNode(classEscape(n), false));
      return true;
    case 'p':
    case 'P':
      if (flags_.unicode) {
        advance(2);
        Ranges r;
        if (!parseProperty(n == 'P', false, r)) return false;
        addTerm(classNode(std::move(r), false));
        return true;
      }
      break;
    case 'k':
      // a named reference with u or when the pattern has named groups, else the identity escape k
      if (flags_.unicode || hasNamedCaptures(false)) {
        advance(2);
        return parseNamedBackref();
      }
      break;
    case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': {
      uint32_t index;
      if (parseBackrefIndex(index)) {
        // inside its own group nothing has been captured yet: the reference matches empty, as in V8
        addTerm(insideCapture(index) ? add(NodeKind::Empty) : add(NodeKind::Backref, index));
        return true;
      }
      if (flags_.unicode) return fail(kInvalidEscape);
      if (n == '8' || n == '9') {
        addChar(n);
        advance(2);
        return true;
      }
      advance();
      addChar(parseOctal());  // Annex B LegacyOctalEscapeSequence
      return true;
    }
    default:
      break;
  }
  uint32_t c;
  if (!parseCharacterEscape(false, c)) return false;
  addChar(c);
  return true;
}

// after \k
bool Parser::parseNamedBackref() {
  if (cur_ != '<') return fail("Invalid named reference");
  advance();
  Name name;
  if (!parseGroupName(name)) return false;
  if (insideCapture(name)) {
    addTerm(add(NodeKind::Empty));
    return true;
  }
  int32_t n = add(NodeKind::Backref);
  namedRefs_.push_back({n, std::move(name)});
  addTerm(n);
  return true;
}

// \N with N at most the number of groups in the whole pattern; else the position is restored
bool Parser::parseBackrefIndex(uint32_t& index) {
  size_t start = curPos_;
  uint32_t value = next() - '0';
  advance(2);
  while (isDigit(cur_)) {
    value = 10 * value + (cur_ - '0');
    if (value > kMaxCaptures) {
      reset(start);
      return false;
    }
    advance();
  }
  if (value > capturesStarted_) {
    if (!scanned_) scanForCaptures(false);
    if (value > captureCount_) {
      reset(start);
      return false;
    }
  }
  index = value;
  return true;
}

// Counts the groups of the rest of the pattern (and notes named ones) without parsing it, as V8's
// ScanForCaptures.
void Parser::scanForCaptures(bool inClass) {
  size_t saved = curPos_;
  uint32_t count = capturesStarted_;
  auto skipClass = [this]() {
    while (cur_ != kEnd) {
      uint32_t c = cur_;
      advance();
      if (c == '\\') advance();
      else if (c == ']') break;
    }
  };
  if (inClass) skipClass();
  while (cur_ != kEnd) {
    uint32_t c = cur_;
    advance();
    if (c == '\\') {
      advance();
    } else if (c == '[') {
      skipClass();
    } else if (c == '(') {
      if (cur_ == '?') {
        advance();
        if (cur_ != '<') continue;
        advance();
        if (cur_ == '=' || cur_ == '!') continue;
        hasNamed_ = true;
      }
      count++;
    }
  }
  captureCount_ = count;
  scanned_ = true;
  reset(saved);
}

bool Parser::hasNamedCaptures(bool inClass) {
  if (hasNamed_ || scanned_) return hasNamed_;
  scanForCaptures(inClass);
  return hasNamed_;
}

// CharacterEscape (and the identity escapes): cur_ is the backslash
bool Parser::parseCharacterEscape(bool inClass, uint32_t& out) {
  advance();
  uint32_t c = cur_;
  switch (c) {
    case 'f': advance(); out = 0x0C; return true;
    case 'n': advance(); out = 0x0A; return true;
    case 'r': advance(); out = 0x0D; return true;
    case 't': advance(); out = 0x09; return true;
    case 'v': advance(); out = 0x0B; return true;
    case 'c': {
      uint32_t ctrl = next();
      uint32_t letter = ctrl & ~uint32_t('A' ^ 'a');
      if (letter >= 'A' && letter <= 'Z') {
        advance(2);
        out = ctrl & 0x1F;
        return true;
      }
      if (flags_.unicode) return fail(kInvalidUnicodeEscape);
      if (inClass && (isDigit(ctrl) || ctrl == '_')) {  // Annex B ClassControlLetter
        advance(2);
        out = ctrl & 0x1F;
        return true;
      }
      out = '\\';  // the backslash is a literal and 'c' is read next
      return true;
    }
    case '0':
      if (!isDigit(next())) {
        advance();
        out = 0;
        return true;
      }
      [[fallthrough]];
    case '1': case '2': case '3': case '4': case '5': case '6': case '7':
      if (flags_.unicode) return fail(kInvalidDecimalEscape);
      out = parseOctal();
      return true;
    case 'x': {
      advance();
      uint32_t v;
      if (parseHex(2, v)) {
        out = v;
        return true;
      }
      if (flags_.unicode) return fail(kInvalidEscape);
      out = 'x';
      return true;
    }
    case 'u': {
      advance();
      uint32_t v;
      if (parseUnicodeEscape(v)) {
        out = v;
        return true;
      }
      if (flags_.unicode) return fail(kInvalidUnicodeEscape);
      out = 'u';
      return true;
    }
    case 'k':
      if (inClass && (flags_.unicode || hasNamedCaptures(true))) return fail(kInvalidEscape);
      break;
    default:
      break;
  }
  if (!flags_.unicode || isSyntaxChar(c) || c == '/') {
    advance();
    out = c;
    return true;
  }
  return fail(kInvalidEscape);
}

// after \u: XXXX, a pair of \uXXXX escapes forming a surrogate pair, or \u{X...} (u mode only)
bool Parser::parseUnicodeEscape(uint32_t& v) {
  if (cur_ == '{' && unicodeMode()) {
    size_t start = curPos_;
    advance();
    if (parseUnlimitedHex(v) && cur_ == '}') {
      advance();
      return true;
    }
    reset(start);
    return false;
  }
  if (!parseHex(4, v)) return false;
  if (unicodeMode() && isLead(v) && cur_ == '\\') {
    size_t start = curPos_;
    if (next() == 'u') {
      advance(2);
      uint32_t trail;
      if (parseHex(4, trail) && isTrail(trail)) {
        v = combineSurrogates(v, trail);
        return true;
      }
    }
    reset(start);
  }
  return true;
}

bool Parser::parseHex(int digits, uint32_t& v) {
  size_t start = curPos_;
  uint32_t x = 0;
  for (int i = 0; i < digits; i++) {
    int d = hexValue(cur_);
    if (d < 0) {
      reset(start);
      return false;
    }
    x = x * 16 + uint32_t(d);
    advance();
  }
  v = x;
  return true;
}

bool Parser::parseUnlimitedHex(uint32_t& v) {
  int d = hexValue(cur_);
  if (d < 0) return false;
  uint32_t x = 0;
  while (d >= 0) {
    x = x * 16 + uint32_t(d);
    if (x > kMaxCodePoint) return false;
    advance();
    d = hexValue(cur_);
  }
  v = x;
  return true;
}

// up to three octal digits with a value below 256; cur_ is the first digit
uint32_t Parser::parseOctal() {
  uint32_t v = cur_ - '0';
  advance();
  if (cur_ >= '0' && cur_ <= '7') {
    v = v * 8 + (cur_ - '0');
    advance();
    if (v < 32 && cur_ >= '0' && cur_ <= '7') {
      v = v * 8 + (cur_ - '0');
      advance();
    }
  }
  return v;
}

bool Parser::parseClass(int32_t& out) {
  advance();  // [
  bool negated = false;
  if (cur_ == '^') {
    negated = true;
    advance();
  }
  Ranges r;
  while (cur_ != kEnd && cur_ != ']') {
    uint32_t c1, c2;
    bool class1, class2;
    if (!parseClassAtom(r, c1, class1)) return false;
    if (cur_ != '-') {
      if (!class1) r.push_back({c1, c1});
      continue;
    }
    advance();
    if (cur_ == kEnd) break;
    if (cur_ == ']') {
      if (!class1) r.push_back({c1, c1});
      r.push_back({'-', '-'});
      break;
    }
    if (!parseClassAtom(r, c2, class2)) return false;
    if (class1 || class2) {
      // Annex B: a class escape at either end makes the '-' a literal
      if (flags_.unicode) return fail("Invalid character class");
      if (!class1) r.push_back({c1, c1});
      r.push_back({'-', '-'});
      if (!class2) r.push_back({c2, c2});
      continue;
    }
    if (c1 > c2) return fail("Range out of order in character class");
    r.push_back({c1, c2});
  }
  if (cur_ == kEnd) return fail("Unterminated character class");
  advance();
  out = classNode(std::move(r), negated);
  return true;
}

// one ClassAtom: a character (c) or a class escape added to r (isClass)
bool Parser::parseClassAtom(Ranges& r, uint32_t& c, bool& isClass) {
  isClass = false;
  if (cur_ != '\\') {
    c = cur_;
    advance();
    return true;
  }
  uint32_t n = next();
  switch (n) {
    case 'b':
      c = 0x08;
      advance(2);
      return true;
    case '-':
      if (flags_.unicode) {
        c = '-';
        advance(2);
        return true;
      }
      break;
    case kEnd:
      return fail("\\ at end of pattern");
    case 'd': case 'D': case 's': case 'S': case 'w': case 'W': {
      Ranges s = classEscape(n);
      r.insert(r.end(), s.begin(), s.end());
      advance(2);
      isClass = true;
      return true;
    }
    case 'p':
    case 'P':
      if (flags_.unicode) {
        advance(2);
        Ranges s;
        if (!parseProperty(n == 'P', true, s)) return false;
        r.insert(r.end(), s.begin(), s.end());
        isClass = true;
        return true;
      }
      break;
    default:
      break;
  }
  return parseCharacterEscape(true, c);
}

// after \p or \P: {name} or {name=value}; names are matched exactly (no loose matching)
bool Parser::parseProperty(bool negate, bool inClass, Ranges& out) {
  const char* err = inClass ? "Invalid property name in character class" : "Invalid property name";
  if (cur_ != '{') return fail(err);
  std::string name, value;
  bool hasValue = false;
  for (advance(); cur_ != '}' && cur_ != '='; advance()) {
    if (!isPropertyNameChar(cur_) || nextPos_ >= src_.size()) return fail(err);
    name += char(cur_);
  }
  if (cur_ == '=') {
    hasValue = true;
    for (advance(); cur_ != '}'; advance()) {
      if (!isPropertyNameChar(cur_) || nextPos_ >= src_.size()) return fail(err);
      value += char(cur_);
    }
  }
  advance();
  if (!ucd::property(name, hasValue ? &value : nullptr, out)) return fail(err);
  if (negate) out = complement(out, kMaxCodePoint);
  return true;
}

}  // namespace

bool parse(const std::u16string& source, const Flags& flags, Ast& ast, std::string& err) {
  ast = Ast();
  ast.flags = flags;
  Parser parser(source, flags, ast, err);
  return parser.run();
}

}  // namespace e7::re
