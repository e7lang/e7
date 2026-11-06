// Unit tests of the regular expression engine (src/regexp): syntax errors with V8's messages, the
// matching semantics (the expected results are V8's, see also the differential test diff.mjs), the API,
// and the resource bounds: the step budget stops catastrophic backtracking and the memory limit stops
// the backtracking stacks.
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "regexp/regexp.h"

namespace {

int failures = 0;
int checks = 0;

void fail(const std::string& what) {
  failures++;
  std::printf("FAILED: %s\n", what.c_str());
}

std::u16string u16(const std::string& s) {
  // UTF-8 to UTF-16 (the test sources are UTF-8)
  std::u16string out;
  for (size_t i = 0; i < s.size();) {
    unsigned char b = static_cast<unsigned char>(s[i]);
    uint32_t c;
    int n;
    if (b < 0x80) c = b, n = 1;
    else if (b < 0xE0) c = b & 0x1F, n = 2;
    else if (b < 0xF0) c = b & 0x0F, n = 3;
    else c = b & 0x07, n = 4;
    for (int k = 1; k < n; k++) c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += n;
    if (c > 0xFFFF) {
      out += char16_t(0xD800 + ((c - 0x10000) >> 10));
      out += char16_t(0xDC00 + ((c - 0x10000) & 0x3FF));
    } else {
      out += char16_t(c);
    }
  }
  return out;
}

std::string capsText(const std::vector<int32_t>& caps) {
  std::string s;
  for (size_t i = 0; i < caps.size(); i++) s += (i ? " " : "") + std::to_string(caps[i]);
  return s;
}

// "null" or the capture indices
std::string run(const std::u16string& pattern, const std::string& flags, const std::u16string& input,
                size_t start = 0, bool sticky = false) {
  std::string err;
  e7::re::ProgramPtr p = e7::re::compile(pattern, flags, err);
  if (!p) return "error: " + err;
  std::vector<int32_t> caps;
  e7::re::Limits lim;
  if (!e7::re::exec(*p, input.data(), input.size(), start, sticky, caps, lim)) return "null";
  return capsText(caps);
}

void expectMatch(const std::string& pattern, const std::string& flags, const std::string& input, const std::string& want,
                 size_t start = 0, bool sticky = false) {
  checks++;
  std::string got = run(u16(pattern), flags, u16(input), start, sticky);
  if (got != want) fail("/" + pattern + "/" + flags + " on \"" + input + "\": got " + got + ", want " + want);
}

void expectError(const std::string& pattern, const std::string& flags, const std::string& msg) {
  checks++;
  std::string err;
  e7::re::ProgramPtr p = e7::re::compile(u16(pattern), flags, err);
  std::string want = msg.rfind("Invalid flags", 0) == 0 ? msg : "Invalid regular expression: /" + pattern + "/" + flags + ": " + msg;
  if (p) fail("/" + pattern + "/" + flags + " compiled, want " + want);
  else if (err != want) fail("/" + pattern + "/" + flags + ": got \"" + err + "\", want \"" + want + "\"");
}

void syntaxErrors() {
  expectError("(", "", "Unterminated group");
  expectError("a)", "", "Unmatched ')'");
  expectError("\\", "", "\\ at end of pattern");
  expectError("*", "", "Nothing to repeat");
  expectError("a**", "", "Nothing to repeat");
  expectError("^*", "", "Nothing to repeat");
  expectError("{1}", "", "Nothing to repeat");
  expectError("a{2,1}", "", "numbers out of order in {} quantifier");
  expectError("{", "u", "Lone quantifier brackets");
  expectError("]", "u", "Lone quantifier brackets");
  expectError("a{1", "u", "Incomplete quantifier");
  expectError("(?=a)*", "u", "Invalid quantifier");
  expectError("(?<=a)*", "", "Invalid quantifier");
  expectError("(?x)", "", "Invalid group");
  expectError("(?i:a)", "", "Invalid group");  // modifiers (ES2025) are not supported, as in Node.js 22
  expectError("(?<1>x)", "", "Invalid capture group name");
  expectError("(?<a>x)(?<a>y)", "", "Duplicate capture group name");
  expectError("(?<a>x)|(?<a>y)", "", "Duplicate capture group name");  // as in Node.js 22
  expectError("(?<a>x)\\k<b>", "", "Invalid named capture referenced");
  expectError("(?<a>x)\\k", "", "Invalid named reference");
  expectError("\\k<a>", "u", "Invalid named capture referenced");
  expectError("[b-a]", "", "Range out of order in character class");
  expectError("[\\d-a]", "u", "Invalid character class");
  expectError("[a", "", "Unterminated character class");
  expectError("\\p{Foo}", "u", "Invalid property name");
  expectError("[\\p{Foo}]", "u", "Invalid property name in character class");
  expectError("\\p{Latin}", "u", "Invalid property name");
  expectError("\\1", "u", "Invalid escape");
  expectError("\\00", "u", "Invalid decimal escape");
  expectError("\\c", "u", "Invalid Unicode escape");
  expectError("\\u{110000}", "u", "Invalid Unicode escape");
  expectError("\\-", "u", "Invalid escape");
  expectError("(?<a>.)[\\k]", "", "Invalid escape");
  expectError("a", "gg", "Invalid flags supplied to RegExp constructor 'gg'");
  expectError("a", "v", "Invalid flags supplied to RegExp constructor 'v'");
  // the flags are printed in V8's order
  std::string err;
  if (e7::re::compile(u"(", "yigmsd", err) || err != "Invalid regular expression: /(/dgimsy: Unterminated group")
    fail("flag order in the message: " + err);
  // lone surrogates in the source are printed as U+FFFD
  if (e7::re::compile(u"\xD800(", "", err) || err != "Invalid regular expression: /\xEF\xBF\xBD(/: Unterminated group")
    fail("lone surrogate in the message: " + err);
  checks += 2;
  // Annex B without u
  expectMatch("]{}", "", "]{}", "0 3");
  expectMatch("a{,2}", "", "a{,2}", "0 5");
  expectMatch("\\8\\q\\c", "", "8q\\c", "0 4");
  expectMatch("[\\c_]\\101\\x4", "", "\x1F" "Ax4", "0 4");
  expectMatch("(?=a)*a", "", "a", "0 1");
}

void semantics() {
  expectMatch("(a|ab)(c|bcd)(d*)", "", "abcd", "0 4 0 1 1 4 4 4");
  expectMatch("(z)((a+)?(b+)?(c))*", "", "zaacbbbcac", "0 10 0 1 8 10 8 9 -1 -1 9 10");
  expectMatch("(a*)*", "", "b", "0 0 -1 -1");
  expectMatch("(a*)?", "", "b", "0 0 -1 -1");
  expectMatch("(?:(a)|b)*", "", "ab", "0 2 -1 -1");
  expectMatch("(.*?)x(.*)", "", "abxcdxef", "0 8 0 2 3 8");
  expectMatch("a{2,3}?", "", "aaaa", "0 2");
  expectMatch("\\1(a)", "", "aa", "0 1 0 1");  // forward reference: empty
  expectMatch("(a\\1)", "", "aa", "0 1 0 1");  // inside its own group: empty
  expectMatch("(?<y>\\d{4})-\\k<y>", "", "x 2024-2024", "2 11 2 6");
  expectMatch("(?=(a+))a*b\\1", "", "baaabac", "3 6 3 4");
  expectMatch("(?!(a))\\1b", "", "b", "0 1 -1 -1");
  expectMatch("(?<=\\$)\\d+(\\.\\d*)?", "", "cost $10.53", "6 11 8 11");
  expectMatch("(?<=(\\d+)(\\d+))$", "", "1053", "4 4 0 1 1 4");
  expectMatch("(?<=\\1(a))b", "", "aab", "2 3 1 2");
  expectMatch("^\\w+$", "m", "one\ntwo", "0 3");
  expectMatch("^two", "m", "one\ntwo", "4 7");
  expectMatch("a.c", "s", "a\nc", "0 3");
  expectMatch("a.c", "", "a\nc", "null");
  expectMatch("\\bfox\\b", "", "the fox", "4 7");
  // case-insensitive: Canonicalize without u, simple case folding with u
  expectMatch("\xC5\xBF", "i", "s", "null");    // U+017F
  expectMatch("\xC5\xBF", "iu", "S", "0 1");
  expectMatch("\\w", "iu", "\xE2\x84\xAA", "0 1");  // U+212A KELVIN SIGN
  expectMatch("\\W", "iu", "\xC5\xBF", "null");
  expectMatch("\\u0390", "iu", "\xE1\xBF\x93", "0 1");  // U+1FD3
  expectMatch("(.)\\1", "iu", "\xF0\x90\x90\x80\xF0\x90\x90\xA8", "0 4 0 2");  // U+10400 U+10428
  // u: code points
  expectMatch("^.$", "u", "\xF0\x9F\x98\x80", "0 2");
  expectMatch("^.$", "", "\xF0\x9F\x98\x80", "null");
  expectMatch("\\ud83d", "u", "\xF0\x9F\x98\x80", "null");
  expectMatch("[^a]", "u", "\xF0\x9F\x98\x80", "0 2");
  expectMatch(".", "u", "\xF0\x9F\x98\x80", "0 2", 1, true);  // sticky inside a pair starts at the lead
  expectMatch("\\B", "u", "A\xF0\x9F\x98\x80" "c", "2 2");  // V8 finds empty matches inside a pair
  // property escapes
  expectMatch("\\p{L}+", "u", "12abc\xCF\x80!", "2 6");
  expectMatch("\\p{Script=Greek}", "u", "a\xCF\x80", "1 2");
  expectMatch("\\p{scx=Grek}", "u", "\xCD\x82", "0 1");  // U+0342, Script=Inherited
  expectMatch("\\P{Lu}", "iu", "A", "0 1");
  // sticky and start
  expectMatch("b", "y", "abc", "null", 0, true);
  expectMatch("b", "y", "abc", "1 2", 1, true);
  expectMatch("b", "", "abcb", "3 4", 2);
  expectMatch("", "", "ab", "2 2", 2);
  expectMatch("a", "", "ab", "null", 3);
}

void api() {
  std::string err;
  e7::re::ProgramPtr p = e7::re::compile(u"(?<year>\\d{4})-(\\d\\d)-(?<day>\\d\\d)", "dgy", err);
  checks++;
  if (!p) {
    fail("compile: " + err);
    return;
  }
  if (e7::re::captureCount(*p) != 3) fail("captureCount");
  const auto& names = e7::re::namedGroups(*p);
  if (names.size() != 2 || names[0].name != "year" || names[0].index != 1 || names[1].name != "day" || names[1].index != 3)
    fail("namedGroups");
  if (!e7::re::hasFlag(*p, 'd') || !e7::re::hasFlag(*p, 'g') || !e7::re::hasFlag(*p, 'y') || e7::re::hasFlag(*p, 'i') ||
      e7::re::hasFlag(*p, 'u'))
    fail("hasFlag");
  e7::re::ProgramPtr q = e7::re::compile(u"(?<π>.)", "", err);
  if (!q || e7::re::namedGroups(*q)[0].name != "\xCF\x80") fail("UTF-8 group name");
  if (std::string(e7::re::unicodeVersion()).empty()) fail("unicodeVersion");
}

// the nesting limit, and no recursion at match time on long inputs
void depth() {
  std::u16string deep;
  for (int i = 0; i < e7::re::kMaxNesting; i++) deep += u"(?:a|";
  deep += u"b";
  for (int i = 0; i < e7::re::kMaxNesting; i++) deep += u")*";
  checks++;
  if (run(deep, "", u"aab") != "0 3") fail("deeply nested pattern");
  expectError(std::string(e7::re::kMaxNesting + 1, '(') + std::string(e7::re::kMaxNesting + 1, ')'), "",
              "Maximum call stack size exceeded");
  std::u16string input(1000000, u'a');
  input += u'c';
  checks++;
  if (run(u"(?:a|b)*c", "", input) != "0 1000001") fail("(?:a|b)*c on a long input");
  if (run(u"((a)|b)+c", "", input) != "0 1000001 999999 1000000 999999 1000000") fail("((a)|b)+c on a long input");
  if (run(u"(?<=^a*)c", "", input) != "1000000 1000001") fail("lookbehind over a long input");
}

struct Budget : std::runtime_error {
  Budget() : std::runtime_error("budget") {}
};

// /(a+)+$/ on "aaa...a!" takes 2^n steps: the budget stops it
void redos() {
  std::string err;
  e7::re::ProgramPtr p = e7::re::compile(u"(a+)+$", "", err);
  std::u16string input(40, u'a');
  input += u'!';
  const uint64_t budget = 2000000;
  uint64_t charged = 0, calls = 0;
  e7::re::Limits lim;
  lim.charge = [&](uint64_t n) {
    charged += n;
    calls++;
    if (charged > budget) throw Budget();
  };
  std::vector<int32_t> caps;
  bool stopped = false;
  try {
    e7::re::exec(*p, input.data(), input.size(), 0, false, caps, lim);
  } catch (const Budget&) {
    stopped = true;
  }
  checks++;
  if (!stopped) fail("ReDoS: the budget did not stop /(a+)+$/");
  else if (charged > budget + 4 * e7::re::kChargeBatch + 64) fail("ReDoS: charged far beyond the budget: " + std::to_string(charged));
  // a match within the budget charges every step, the remainder included
  charged = 0;
  e7::re::ProgramPtr q = e7::re::compile(u"b", "", err);
  std::u16string small = u"aab";
  checks++;
  if (!e7::re::exec(*q, small.data(), small.size(), 0, false, caps, lim) || charged == 0) fail("steps of a short match are charged");
}

struct OutOfMemory : std::runtime_error {
  OutOfMemory() : std::runtime_error("memory") {}
};

// every choice point of (?:a|b)* stays until the end: the backtracking stack grows with the input
void memory() {
  std::string err;
  e7::re::ProgramPtr p = e7::re::compile(u"(?:a|b)*c", "", err);
  std::u16string input(2000000, u'a');
  const size_t limit = 1 << 20;
  size_t last = 0;
  bool monotonic = true;
  e7::re::Limits lim;
  lim.reserve = [&](size_t bytes) {
    if (bytes < last) monotonic = false;
    last = bytes;
    if (bytes > limit) throw OutOfMemory();
  };
  std::vector<int32_t> caps;
  bool stopped = false;
  try {
    e7::re::exec(*p, input.data(), input.size(), 0, false, caps, lim);
  } catch (const OutOfMemory&) {
    stopped = true;
  }
  checks++;
  if (!stopped) fail("memory: the limit did not stop the stack");
  if (!monotonic) fail("memory: reserve sizes decrease");
  // with enough memory the same match succeeds
  input += u'c';
  checks++;
  if (!e7::re::exec(*p, input.data(), input.size(), 0, false, caps, e7::re::Limits()) || caps[1] != int32_t(input.size()))
    fail("memory: the match with no limit");
}

// compilation under limits: the work of a large pattern is charged and its memory reserved, so a
// throwing callback stops it; repeated classes and properties are built once
void compileLimits() {
  std::u16string big;
  for (int i = 0; i < 100000; i++) big += u"[^a]";
  uint64_t charged = 0;
  size_t reserved = 0;
  e7::re::Limits lim;
  lim.charge = [&](uint64_t n) { charged += n; };
  lim.reserve = [&](size_t b) { reserved = b; };
  std::string err;
  e7::re::ProgramPtr p = e7::re::compile(big, "iu", err, &lim);
  checks++;
  if (!p || charged < 100000 || reserved < 1000000) fail("compile charges its work and reserves its memory");
  lim.charge = [&](uint64_t n) {
    charged += n;
    if (charged > 50000) throw Budget();
  };
  charged = 0;
  bool stopped = false;
  try {
    e7::re::compile(big, "iu", err, &lim);
  } catch (const Budget&) {
    stopped = true;
  }
  checks++;
  if (!stopped) fail("a throwing charge stops the compilation");
  lim.charge = nullptr;
  lim.reserve = [](size_t b) {
    if (b > (1u << 20)) throw OutOfMemory();
  };
  stopped = false;
  try {
    e7::re::compile(big, "", err, &lim);
  } catch (const OutOfMemory&) {
    stopped = true;
  }
  checks++;
  if (!stopped) fail("a throwing reserve stops the compilation");
  // a match's setup is charged by its registers: a pattern with many groups costs more per exec
  std::u16string many = u"b";
  for (int i = 0; i < 20000; i++) many += u"()";
  e7::re::ProgramPtr groups = e7::re::compile(many, "", err);
  charged = 0;
  lim.charge = [&](uint64_t n) { charged += n; };
  lim.reserve = nullptr;
  std::u16string in = u"a";
  std::vector<int32_t> caps;
  checks++;
  if (!groups || e7::re::exec(*groups, in.data(), in.size(), 0, true, caps, lim) || charged < 5000)
    fail("the setup of a match with many registers is charged");
}

// one program, several threads
void threads() {
  std::string err;
  e7::re::ProgramPtr p = e7::re::compile(u"(\\w+)@(\\w+)\\.com", "i", err);
  std::u16string input = u"mail me: someone@example.com, please";
  std::vector<std::thread> ts;
  std::vector<int> ok(8, 0);
  for (int t = 0; t < 8; t++)
    ts.emplace_back([&, t]() {
      std::vector<int32_t> caps;
      for (int i = 0; i < 2000; i++)
        if (!e7::re::exec(*p, input.data(), input.size(), 0, false, caps, e7::re::Limits()) || caps[0] != 9) return;
      ok[size_t(t)] = 1;
    });
  for (auto& t : ts) t.join();
  checks++;
  for (int v : ok)
    if (!v) {
      fail("threads");
      break;
    }
}

}  // namespace

int main() {
  syntaxErrors();
  semantics();
  api();
  depth();
  redos();
  memory();
  compileLimits();
  threads();
  if (failures) {
    std::printf("regexp_test: %d of %d checks FAILED\n", failures, checks);
    return 1;
  }
  std::printf("regexp_test: %d checks passed (Unicode %s)\n", checks, e7::re::unicodeVersion());
  return 0;
}
