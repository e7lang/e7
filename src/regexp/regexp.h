// ECMAScript regular expressions for e7vm (docs/DESIGN-0.3.md, D4).
//
// A pattern is parsed and compiled to a small program, which a backtracking matcher with an explicit
// stack runs on UTF-16 input: no C++ recursion depends on the input or (beyond a fixed nesting limit
// checked when compiling) on the pattern. Semantics are ES2024's without the v flag, as V8 implements
// them in Node.js 22: the Annex B syntax without u, and V8's error messages.
//
// The library is self-contained (no VM types). Programs are immutable after compile(): one Program
// can be run by several threads at once. The only exceptions that leave exec() are those thrown by the
// Limits callbacks and std::bad_alloc; compile() reports errors through its err argument.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace e7::re {

struct Program;  // a compiled pattern (opaque; deleted through std::unique_ptr)
void destroy(Program* p);
struct ProgramDeleter {
  void operator()(Program* p) const { destroy(p); }
};
using ProgramPtr = std::unique_ptr<Program, ProgramDeleter>;

struct NamedGroup {
  std::string name;  // UTF-8
  uint32_t index;    // >= 1
};

// Compiles source (the pattern text between the slashes, UTF-16) with flags (any subset of "dgimsuy",
// each at most once). On an error returns nullptr and sets err to V8's message:
//   - "Invalid flags supplied to RegExp constructor 'gg'" for bad flags (the v flag is not supported and
//     is reported this way too);
//   - "Invalid regular expression: /(/: Unterminated group" for a syntax error, with the flags after the
//     second slash in V8's order ("dgimsuy") and the source in UTF-8 (lone surrogates as U+FFFD);
//   - nesting deeper than kMaxNesting groups gives "...: Maximum call stack size exceeded" (V8 accepts
//     deeper patterns; this bound keeps the compiler's recursion small).
ProgramPtr compile(const std::u16string& source, const std::string& flags, std::string& err);
constexpr int kMaxNesting = 256;

uint32_t captureCount(const Program& p);                       // capture groups, not counting group 0
const std::vector<NamedGroup>& namedGroups(const Program& p);  // in source order
bool hasFlag(const Program& p, char f);                         // one of "dgimsuy"
const std::string& flagText(const Program& p);                  // the flags in V8's order ("dgimsuy")
size_t programSize(const Program& p);                           // approximate bytes held by the program

// Resource limits: charge(n) is called for every batch of n matcher steps and may throw to stop the
// match (the VM throws its budget error); reserve(bytes) is called before the backtracking stacks grow
// to `bytes` in total and may throw (the VM checks its memory limit). Both may be empty functions.
struct Limits {
  std::function<void(uint64_t)> charge;
  std::function<void(size_t)> reserve;
};
constexpr uint64_t kChargeBatch = 1024;

// Tries to match input[0..len) starting at index `start`: searches forward from start, or only at start
// when sticky is true (the g / y handling of lastIndex is the caller's). With the u flag, a start in the
// middle of a surrogate pair moves back to the pair's lead surrogate, as V8 does. On a match fills caps
// with 2 * (captureCount + 1) UTF-16 indices (start, end per group; -1 for groups that did not
// participate) and returns true. Returns false when start > len, and for inputs of 2^31 - 1 code units
// or more (E7 strings are shorter).
bool exec(const Program& p, const char16_t* input, size_t len, size_t start, bool sticky,
          std::vector<int32_t>& caps, const Limits& lim);

// Unicode version of the property and case tables (src/regexp/unicode_tables.inc).
const char* unicodeVersion();

}  // namespace e7::re
