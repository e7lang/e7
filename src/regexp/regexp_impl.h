// Internal declarations of the regular expression engine (src/regexp): character sets, the Unicode
// tables, the parsed pattern (Ast) and the compiled program the matcher runs.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "regexp/regexp.h"

namespace e7::re {

constexpr uint32_t kMaxCodePoint = 0x10FFFF;
constexpr int32_t kInfinity = INT32_MAX;  // an unbounded quantifier maximum, as V8's kInfinity
constexpr uint32_t kMaxCaptures = (1u << 15) - 1;  // as V8: more groups are "Too many captures"

inline bool isLead(uint32_t c) { return c >= 0xD800 && c <= 0xDBFF; }
inline bool isTrail(uint32_t c) { return c >= 0xDC00 && c <= 0xDFFF; }
inline uint32_t combineSurrogates(uint32_t lead, uint32_t trail) {
  return 0x10000 + ((lead - 0xD800) << 10) + (trail - 0xDC00);
}
inline bool isLineTerminator(uint32_t c) { return c == 0x0A || c == 0x0D || c == 0x2028 || c == 0x2029; }
inline bool isBasicWordChar(uint32_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// The work of compile(): steps charged in batches, memory reserved as the parsed pattern and the program
// grow (the callbacks may throw to stop the compilation)
struct CompileWork {
  const Limits* lim = nullptr;
  uint64_t steps = 0;
  size_t bytes = 0, reported = 0;
  void charge(uint64_t n) {
    steps += n;
    if (steps >= kChargeBatch) flush();
  }
  void flush() {
    uint64_t n = steps;
    steps = 0;
    if (n && lim && lim->charge) lim->charge(n);
  }
  void grow(size_t n) {
    bytes += n;
    if (bytes >= reported + 16384 && lim && lim->reserve) {
      reported = bytes;
      lim->reserve(bytes);
    }
  }
};

// ---------------------------------------------------------------- character sets
struct Range {
  uint32_t lo, hi;  // inclusive
};
using Ranges = std::vector<Range>;  // after normalize(): sorted, disjoint and not adjacent

void normalize(Ranges& r);
Ranges complement(const Ranges& r, uint32_t maxChar);
Ranges symmetricDifference(const Ranges& a, const Ranges& b);
bool contains(const Ranges& r, uint32_t c);  // r normalized

// A set prepared for matching: a bitmap for ASCII, binary search above.
struct CharClass {
  uint32_t ascii[4] = {0, 0, 0, 0};
  Ranges ranges;  // normalized; only consulted for c >= 128
  explicit CharClass(const Ranges& r);
  bool contains(uint32_t c) const {
    if (c < 128) return (ascii[c >> 5] >> (c & 31)) & 1;
    size_t lo = 0, hi = ranges.size();
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      if (c > ranges[mid].hi) lo = mid + 1;
      else if (c < ranges[mid].lo) hi = mid;
      else return true;
    }
    return false;
  }
};

// ---------------------------------------------------------------- Unicode data (unicode.cpp)
namespace ucd {
// \p{name} (value == nullptr) or \p{name=value}; false for names V8 rejects
bool property(const std::string& name, const std::string* value, Ranges& out);
// case-insensitive representative: Canonicalize without u (code units), simple case folding with u
uint32_t canonicalize(uint32_t c, bool unicode);
// the code points that are case-insensitively equal to c (c included)
void caseEquivalents(uint32_t c, bool unicode, Ranges& out);
// adds every code point case-insensitively equal to a member (the closure CharacterClass matching uses);
// returns the work done (lookups)
uint64_t closeOverCase(Ranges& set, bool unicode);
bool isIdStart(uint32_t c);     // RegExpIdentifierStart: ID_Start, $ and _
bool isIdContinue(uint32_t c);  // RegExpIdentifierPart: ID_Continue, $, ZWNJ and ZWJ
const char* version();
}  // namespace ucd

// ---------------------------------------------------------------- parsed pattern (parser.cpp)
struct Flags {
  bool hasIndices = false, global = false, ignoreCase = false, multiline = false;
  bool dotAll = false, unicode = false, sticky = false;
};

enum class NodeKind : uint8_t {
  Empty,
  Char,     // value: code point (a code unit without u)
  Class,    // value: index into Ast::classes (already case-closed and negated as needed)
  Dot,
  Seq,      // kids
  Alt,      // kids
  Capture,  // value: group index; child
  Group,    // (?:...); child
  Backref,  // value: group index
  Bol,
  Eol,
  WordBoundary,
  NotWordBoundary,
  Look,    // child; negative, behind
  Repeat,  // child; min, max (kInfinity: unbounded), greedy
};

struct Node {
  NodeKind kind = NodeKind::Empty;
  bool greedy = true;
  bool negative = false;
  bool behind = false;
  uint32_t value = 0;
  int32_t min = 0, max = 0;
  int32_t child = -1;
  std::vector<int32_t> kids;
};

struct Ast {
  Flags flags;
  std::vector<Node> nodes;
  std::vector<Ranges> classes;
  int32_t root = -1;
  uint32_t captureCount = 0;
  std::vector<NamedGroup> names;
};

// On a syntax error returns false and sets err to V8's message text (without the "Invalid regular
// expression: /.../: " prefix).
bool parse(const std::u16string& source, const Flags& flags, Ast& ast, std::string& err, CompileWork& work);

// ---------------------------------------------------------------- program (compiler.cpp)
enum Op : uint8_t {
  OP_CHAR,            // a: character
  OP_STRING,          // a: offset in Program::strings, b: length in code units (no ignoreCase)
  OP_CLASS,           // a: index in Program::classes
  OP_ANY,             // any character but a line terminator
  OP_ANY_ALL,         // any character
  OP_FAIL,
  OP_BOL, OP_BOL_M, OP_EOL, OP_EOL_M,
  OP_WORD_BOUNDARY,   // flags & kExtendedWord: \w with u and i (adds U+017F, U+212A)
  OP_NOT_WORD_BOUNDARY,
  OP_SPLIT_NEXT,      // continue at pc + 1, alternative a
  OP_SPLIT_JUMP,      // continue at a, alternative pc + 1
  OP_JMP,             // a
  OP_GROUP_OPEN,      // a: group (remembers the position in its start register)
  OP_GROUP_CLOSE,     // a: group (sets the capture from the start register and the position)
  OP_RESET,           // groups a..b become undefined (each quantifier iteration)
  OP_SAVE_POS,        // register a = position
  OP_CHECK_PROGRESS,  // fail when position == register a (the empty check of ES2024 RepeatMatcher)
  OP_LOOP_INIT,       // counter register a = 0
  OP_LOOP,            // counter a, min b, max c, exit d; body at pc + 1
  OP_LOOP_END,        // counter a, position register b (-1: no empty check), min c, head d
  OP_BACKREF,         // a: group
  OP_LOOK,            // a: register for the choice stack height, b: continuation (negative)
  OP_LOOK_END,        // a: the register of the matching OP_LOOK
  OP_STAR,            // a: OP_CHAR / OP_CLASS / OP_ANY / OP_ANY_ALL with argument b; min c, max d
  OP_MATCH,
};

enum : uint8_t {
  kBackward = 1,      // reads to the left (lookbehind)
  kGreedy = 2,        // OP_LOOP, OP_STAR
  kNegative = 4,      // OP_LOOK, OP_LOOK_END
  kIgnoreCase = 8,    // OP_BACKREF
  kExtendedWord = 16, // word boundaries
};

struct Inst {
  uint8_t op = OP_FAIL;
  uint8_t flags = 0;
  int32_t a = 0, b = 0, c = 0, d = 0;
};

}  // namespace e7::re

namespace e7::re {

struct Program {
  Flags flags;
  std::string flagText;  // canonical order
  uint32_t captureCount = 0;
  std::vector<NamedGroup> names;
  std::vector<Inst> code;
  std::vector<CharClass> classes;
  std::u16string strings;
  int32_t registerCount = 0;  // captures (2 per group), group start registers, loop and look registers
  // search acceleration: a match must start with a code unit of `first` (when hasFirst), or only at
  // index 0 (anchored: begins with ^ without the m flag)
  bool hasFirst = false;
  bool anchored = false;
  int32_t firstUnit = -1;  // when the first set is one code unit
  CharClass first{Ranges{}};
};

// Compiles the parsed pattern; false when the program would be too large.
bool generate(const Ast& ast, Program& p, CompileWork& work);

}  // namespace e7::re
