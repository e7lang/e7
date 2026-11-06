// Line-oriented driver of the regular expression engine for tests/regexp/diff.mjs (the differential
// test against Node.js). Each input line is one case:
//
//   flags TAB pattern TAB input TAB start TAB sticky(0/1)
//
// with pattern and input as UTF-16 code units in hex (4 digits each). Each output line is
//
//   E TAB message                      the pattern does not compile
//   N                                  no match
//   M TAB caps TAB names               a match: the 2 * (groups + 1) indices, space-separated;
//                                      names as name=index, comma-separated
//   B                                  the step budget (--budget, default 50M steps) ran out
//
// The line "?version" prints the Unicode version of the tables.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "regexp/regexp.h"

namespace {

struct BudgetExceeded {};

std::u16string fromHex(const std::string& s) {
  std::u16string out;
  for (size_t i = 0; i + 4 <= s.size(); i += 4) out += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  uint64_t budget = 50000000;
  for (int i = 1; i < argc; i++)
    if (std::strcmp(argv[i], "--budget") == 0 && i + 1 < argc) budget = std::strtoull(argv[++i], nullptr, 10);
  std::ios::sync_with_stdio(false);
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line == "?version") {
      std::cout << e7::re::unicodeVersion() << '\n' << std::flush;
      continue;
    }
    std::string field[5];
    size_t at = 0;
    for (int f = 0; f < 5; f++) {
      size_t tab = line.find('\t', at);
      field[f] = line.substr(at, tab == std::string::npos ? std::string::npos : tab - at);
      at = tab == std::string::npos ? line.size() : tab + 1;
    }
    std::string err;
    e7::re::ProgramPtr p = e7::re::compile(fromHex(field[1]), field[0], err);
    if (!p) {
      std::cout << "E\t" << err << '\n';
      continue;
    }
    std::u16string input = fromHex(field[2]);
    uint64_t used = 0;
    e7::re::Limits lim;
    lim.charge = [&used, budget](uint64_t n) {
      used += n;
      if (used > budget) throw BudgetExceeded();
    };
    std::vector<int32_t> caps;
    bool found;
    try {
      found = e7::re::exec(*p, input.data(), input.size(), std::strtoul(field[3].c_str(), nullptr, 10),
                           field[4] == "1", caps, lim);
    } catch (const BudgetExceeded&) {
      std::cout << "B\n";
      continue;
    }
    if (!found) {
      std::cout << "N\n";
      continue;
    }
    std::cout << "M\t";
    for (size_t i = 0; i < caps.size(); i++) std::cout << (i ? " " : "") << caps[i];
    std::cout << '\t';
    bool first = true;
    for (const auto& g : e7::re::namedGroups(*p)) {
      std::cout << (first ? "" : ",") << g.name << '=' << g.index;
      first = false;
    }
    std::cout << '\n';
  }
  return 0;
}
