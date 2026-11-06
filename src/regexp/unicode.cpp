// Character sets and the Unicode data of the regular expression engine: property sets for \p{...},
// case-insensitive equivalence (Canonicalize without u, simple case folding with u) and the identifier
// characters of group names. The tables are generated from Node.js (scripts/gen-regexp-unicode.mjs).
#include <algorithm>
#include <cstring>
#include <map>

#include "regexp/regexp_impl.h"

namespace e7::re {

void normalize(Ranges& r) {
  if (r.empty()) return;
  std::sort(r.begin(), r.end(), [](const Range& x, const Range& y) { return x.lo < y.lo; });
  size_t n = 0;
  for (size_t i = 1; i < r.size(); i++) {
    if (r[i].lo <= r[n].hi + 1) r[n].hi = std::max(r[n].hi, r[i].hi);
    else r[++n] = r[i];
  }
  r.resize(n + 1);
}

Ranges complement(const Ranges& r, uint32_t maxChar) {
  Ranges out;
  uint32_t next = 0;
  for (const Range& x : r) {
    if (x.lo > maxChar) break;
    if (x.lo > next) out.push_back({next, x.lo - 1});
    next = x.hi + 1;
  }
  if (next <= maxChar) out.push_back({next, maxChar});
  return out;
}

Ranges symmetricDifference(const Ranges& a, const Ranges& b) {
  // walk the boundaries of both lists: a point is in the result when it is inside exactly one of them
  std::vector<std::pair<uint32_t, int>> ev;
  for (const Ranges* s : {&a, &b})
    for (const Range& x : *s) {
      ev.push_back({x.lo, 1});
      ev.push_back({x.hi + 1, -1});
    }
  std::sort(ev.begin(), ev.end());
  Ranges out;
  int depth = 0;
  uint32_t start = 0;
  for (size_t i = 0; i < ev.size();) {
    uint32_t p = ev[i].first;
    bool before = depth % 2;
    while (i < ev.size() && ev[i].first == p) depth += ev[i++].second;
    bool after = depth % 2;
    if (!before && after) start = p;
    else if (before && !after) out.push_back({start, p - 1});
  }
  normalize(out);
  return out;
}

bool contains(const Ranges& r, uint32_t c) {
  auto it = std::upper_bound(r.begin(), r.end(), c, [](uint32_t v, const Range& x) { return v < x.lo; });
  return it != r.begin() && c <= (it - 1)->hi;
}

CharClass::CharClass(const Ranges& r) : ranges(r) {
  for (const Range& x : r)
    for (uint32_t c = x.lo; c <= x.hi && c < 128; c++) ascii[c >> 5] |= 1u << (c & 31);
}

namespace ucd {
namespace {

struct GcName {
  const char* name;
  uint32_t mask;  // bits of kGcLeafSets
};
struct ScriptName {
  const char* name;
  uint16_t sc;       // Script set
  uint16_t scxDiff;  // Script_Extensions = Script xor this set
};
enum : uint16_t { kSetAny = 0xFFFF, kSetAscii = 0xFFFE, kSetAssigned = 0xFFFD };
struct BinaryName {
  const char* name;
  uint16_t set;
};
struct FoldRun {
  uint32_t lo;
  uint16_t count;
  uint8_t stride;
  int32_t delta;
};

#include "regexp/unicode_tables.inc"

Ranges decode(uint16_t set) {
  Ranges out;
  const uint8_t* p = kSetData + kSetOffsets[set];
  const uint8_t* end = kSetData + kSetOffsets[set + 1];
  auto varint = [&p]() {
    uint32_t v = 0;
    for (int shift = 0;; shift += 7) {
      uint8_t b = *p++;
      v |= uint32_t(b & 0x7F) << shift;
      if (!(b & 0x80)) return v;
    }
  };
  uint32_t prev = 0;
  while (p < end) {
    uint32_t lo = prev + varint();
    uint32_t hi = lo + varint();
    out.push_back({lo, hi});
    prev = hi + 1;
  }
  return out;
}

Ranges generalCategory(uint32_t mask) {
  Ranges out;
  for (size_t i = 0; i < sizeof(kGcLeafSets) / sizeof(kGcLeafSets[0]); i++)
    if (mask & (1u << i)) {
      Ranges s = decode(kGcLeafSets[i]);
      out.insert(out.end(), s.begin(), s.end());
    }
  normalize(out);
  return out;
}

Ranges binarySet(uint16_t set) {
  if (set == kSetAny) return {{0, kMaxCodePoint}};
  if (set == kSetAscii) return {{0, 0x7F}};
  if (set == kSetAssigned) {
    for (const GcName& g : kGcNames)
      if (std::strcmp(g.name, "Cn") == 0) return complement(generalCategory(g.mask), kMaxCodePoint);
  }
  return decode(set);
}

template <class T, size_t N>
const T* findName(const T (&table)[N], const std::string& name) {
  for (const T& e : table)
    if (name == e.name) return &e;
  return nullptr;
}

uint32_t foldWith(const FoldRun* runs, size_t n, uint32_t c) {
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (runs[mid].lo <= c) lo = mid + 1;
    else hi = mid;
  }
  if (lo == 0) return c;
  const FoldRun& r = runs[lo - 1];
  uint32_t off = c - r.lo;
  if (off % r.stride != 0 || off / r.stride >= r.count) return c;
  return uint32_t(int32_t(c) + r.delta);
}

// The equivalence classes with more than one member, from the representative runs: built once
// (thread-safe static initialization) and kept for the process.
struct CaseClasses {
  std::vector<uint32_t> reps;                  // sorted representatives
  std::vector<std::vector<uint32_t>> members;  // per representative, sorted
  std::vector<std::pair<uint32_t, uint32_t>> byMember;  // (member, class), sorted by member

  CaseClasses(const FoldRun* runs, size_t n) {
    std::map<uint32_t, std::vector<uint32_t>> m;
    for (size_t i = 0; i < n; i++)
      for (uint32_t k = 0; k < runs[i].count; k++) {
        uint32_t c = runs[i].lo + k * runs[i].stride;
        m[uint32_t(int32_t(c) + runs[i].delta)].push_back(c);
      }
    for (auto& [rep, list] : m) {
      if (foldWith(runs, n, rep) == rep) list.push_back(rep);
      std::sort(list.begin(), list.end());
      reps.push_back(rep);
      for (uint32_t c : list) byMember.push_back({c, uint32_t(members.size())});
      members.push_back(std::move(list));
    }
    std::sort(byMember.begin(), byMember.end());
  }
  const std::vector<uint32_t>* classOf(uint32_t rep) const {
    auto it = std::lower_bound(reps.begin(), reps.end(), rep);
    return it != reps.end() && *it == rep ? &members[it - reps.begin()] : nullptr;
  }
};

const CaseClasses& caseClasses(bool unicode) {
  static const CaseClasses nonUnicode(kFoldNonUnicode, sizeof(kFoldNonUnicode) / sizeof(kFoldNonUnicode[0]));
  static const CaseClasses withUnicode(kFoldUnicode, sizeof(kFoldUnicode) / sizeof(kFoldUnicode[0]));
  return unicode ? withUnicode : nonUnicode;
}

Ranges binaryByName(const char* name) {
  const BinaryName* e = findName(kBinaryNames, name);
  return e ? binarySet(e->set) : Ranges{};
}

}  // namespace

bool property(const std::string& name, const std::string* value, Ranges& out) {
  if (!value) {
    if (const GcName* g = findName(kGcNames, name)) {
      out = generalCategory(g->mask);
      return true;
    }
    if (const BinaryName* b = findName(kBinaryNames, name)) {
      out = binarySet(b->set);
      return true;
    }
    return false;
  }
  if (name == "General_Category" || name == "gc") {
    const GcName* g = findName(kGcNames, *value);
    if (!g) return false;
    out = generalCategory(g->mask);
    return true;
  }
  bool scx = name == "Script_Extensions" || name == "scx";
  if (!scx && name != "Script" && name != "sc") return false;
  const ScriptName* s = findName(kScriptNames, *value);
  if (!s) return false;
  out = decode(s->sc);
  if (scx) out = symmetricDifference(out, decode(s->scxDiff));
  return true;
}

uint32_t canonicalize(uint32_t c, bool unicode) {
  if (unicode) return foldWith(kFoldUnicode, sizeof(kFoldUnicode) / sizeof(kFoldUnicode[0]), c);
  return foldWith(kFoldNonUnicode, sizeof(kFoldNonUnicode) / sizeof(kFoldNonUnicode[0]), c);
}

void caseEquivalents(uint32_t c, bool unicode, Ranges& out) {
  const std::vector<uint32_t>* cls = caseClasses(unicode).classOf(canonicalize(c, unicode));
  if (!cls) {
    out.push_back({c, c});
    return;
  }
  for (uint32_t m : *cls) out.push_back({m, m});
}

uint64_t closeOverCase(Ranges& set, bool unicode) {
  const CaseClasses& cc = caseClasses(unicode);
  Ranges add;
  uint64_t size = 0, work = set.size();
  for (const Range& r : set) size += uint64_t(r.hi) - r.lo + 1;
  if (size <= 64) {
    // few characters: look each one up
    for (const Range& r : set)
      for (uint32_t c = r.lo; c <= r.hi; c++) caseEquivalents(c, unicode, add);
    work += size * 8;
  } else {
    // many: test the members of every class against the set
    for (const auto& [member, cls] : cc.byMember)
      if (contains(set, member))
        for (uint32_t m : cc.members[cls]) add.push_back({m, m});
    work += cc.byMember.size() * 4;
  }
  set.insert(set.end(), add.begin(), add.end());
  normalize(set);
  return work + add.size();
}

bool isIdStart(uint32_t c) {
  static const Ranges ids = binaryByName("ID_Start");
  return c == '$' || c == '_' || contains(ids, c);
}

bool isIdContinue(uint32_t c) {
  static const Ranges idc = binaryByName("ID_Continue");
  return c == '$' || c == 0x200C || c == 0x200D || contains(idc, c);
}

const char* version() { return kUnicodeVersion; }

}  // namespace ucd
}  // namespace e7::re
