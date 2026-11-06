// Regular expressions (0.3, DESIGN-0.3.md D4): RegExp objects and the String methods that take one
// (match, matchAll, replace, replaceAll, split, search), following the ES2024 algorithms
// (RegExpBuiltinExec, @@replace, @@split, ...). The matcher is src/regexp; its steps are charged to the
// instruction budget and its backtracking stacks to the memory limit, so a catastrophic pattern ends
// with E7_ERR_BUDGET or E7_ERR_MEMORY like any other script work.
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "../regexp/regexp.h"
#include "util.h"

namespace e7 {
namespace {

RegExpObj* Re(VM& vm, Slot s) {
  if (E7_UNLIKELY(!s.r || s.r->type != OT::RegExp))
    vm.error(s.r ? "expected a RegExp" : "cannot read properties of null or undefined (RegExp)");
  return static_cast<RegExpObj*>(s.r);
}

re::Limits limits(VM& vm) { return vm.regexpLimits(); }

// A result string built in C++: its size counts against the memory limit (and the string length limit) as
// it grows, and the copying against the budget
struct Builder {
  VM& vm;
  std::u16string out;
  size_t checked = 0;
  explicit Builder(VM& v) : vm(v) {}
  void grew(size_t n) {
    vm.chargeWork(n / 16 + 1);
    if (out.size() > 0x3FFFFFFF) vm.error("string too long");
    if (out.size() >= checked + 4096) {
      checked = out.size();
      vm.checkTemp(out.capacity() * sizeof(char16_t));
    }
  }
  void append(const char16_t* p, size_t n) {
    if (out.size() + n > 0x3FFFFFFF) vm.error("string too long");
    vm.checkTemp((out.size() + n) * sizeof(char16_t));
    out.append(p, n);
    grew(n);
  }
};

// ToLength
double toLength(double d) {
  d = toInteger(d);
  if (d <= 0) return 0;
  return d > 9007199254740991.0 ? 9007199254740991.0 : d;
}

// AdvanceStringIndex
double advance(const StrObj* s, double i, bool unicode) {
  if (!unicode || i + 1 >= s->len) return i + 1;
  const char16_t* c = s->chars();
  size_t k = (size_t)i;
  return (c[k] >= 0xD800 && c[k] <= 0xDBFF && c[k + 1] >= 0xDC00 && c[k + 1] <= 0xDFFF) ? i + 2 : i + 1;
}

bool flag(const RegExpObj* re, char f) { return re::hasFlag(*re->prog, f); }

// RegExpBuiltinExec: the match at lastIndex (g / y) or the first one (otherwise), updating lastIndex
bool builtinExec(VM& vm, RegExpObj* re, const StrObj* s, std::vector<int32_t>& caps) {
  bool g = flag(re, 'g'), y = flag(re, 'y');
  double li = g || y ? toLength(re->lastIndex) : 0;
  if (li > s->len) {
    if (g || y) re->lastIndex = 0;
    return false;
  }
  re::Limits lim = limits(vm);
  if (!re::exec(*re->prog, s->chars(), s->len, (size_t)li, y, caps, lim)) {
    if (g || y) re->lastIndex = 0;
    return false;
  }
  if (g || y) re->lastIndex = caps[1];
  return true;
}

Obj* substr(VM& vm, const StrObj* s, int32_t from, int32_t to) {
  if (from < 0) return vm.undef();
  return vm.newString(s->chars() + from, (size_t)(to - from));
}

// the groups object of a match: a record of the VM class for these names, a property per name
Obj* groupsObject(VM& vm, const re::Program& p, const std::vector<int32_t>& caps, const StrObj* s, bool indices) {
  const auto& names = re::namedGroups(p);
  if (names.empty()) return vm.undef();
  std::vector<std::string> keys;
  for (auto& g : names) keys.push_back(g.name);
  InstObj* o = vm.newInstance(vm.groupsClass(keys));
  for (size_t k = 0; k < names.size(); k++) o->fields()[k].r = vm.undef();
  for (size_t k = 0; k < names.size(); k++) {
    uint32_t gi = names[k].index;
    int32_t from = caps[2 * gi], to = caps[2 * gi + 1];
    Obj* v;
    if (from < 0) v = vm.undef();
    else if (!indices) v = substr(vm, s, from, to);
    else {
      ArrObj* pair = vm.newArray(VT_NUMBER, 2);
      vm.arrayPush(pair, slotF(from));
      vm.arrayPush(pair, slotF(to));
      v = pair;
    }
    o->fields()[k].r = v;
  }
  return o;
}

// the result array of exec / match: the matched text and the groups, index, input, groups (and indices
// with the d flag)
ArrObj* matchArray(VM& vm, const RegExpObj* re, StrObj* s, const std::vector<int32_t>& caps) {
  const re::Program& p = *re->prog;
  uint32_t n = (uint32_t)caps.size() / 2;
  ArrObj* a = vm.newMatchArray(VT_REF, n, kArrFlagMatch);
  for (uint32_t i = 0; i < n; i++) vm.arrayPush(a, slotR(substr(vm, s, caps[2 * i], caps[2 * i + 1])));
  MatchExtra* x = matchExtra(a);
  x->index = caps[0];
  x->input = s;
  x->groups = groupsObject(vm, p, caps, s, false);
  if (re::hasFlag(p, 'd')) {
    ArrObj* ind = vm.newMatchArray(VT_REF, n, kArrFlagIndices);
    for (uint32_t i = 0; i < n; i++) {
      Obj* v = vm.undef();
      if (caps[2 * i] >= 0) {
        ArrObj* pair = vm.newArray(VT_NUMBER, 2);
        vm.arrayPush(pair, slotF(caps[2 * i]));
        vm.arrayPush(pair, slotF(caps[2 * i + 1]));
        v = pair;
      }
      vm.arrayPush(ind, slotR(v));
    }
    matchExtra(ind)->groups = groupsObject(vm, p, caps, s, true);
    matchExtra(a)->indices = ind;
  }
  return a;
}

// EscapeRegExpPattern, as V8 writes it: '/' outside a class and line terminators are escaped
std::u16string escapeSource(const StrObj* src) {
  const char16_t* c = src->chars();
  uint32_t n = src->len;
  if (n == 0) return u"(?:)";
  auto lineTerm = [](char16_t x) { return x == '\n' || x == '\r' || x == 0x2028 || x == 0x2029; };
  std::u16string out;
  bool inClass = false;
  for (uint32_t i = 0; i < n; i++) {
    char16_t ch = c[i];
    if (ch == '\\') {
      if (i + 1 < n && lineTerm(c[i + 1])) continue;  // the line terminator is escaped next
      out += c[i++];
      if (i == n) break;
    } else if (ch == '/' && !inClass) {
      out += u'\\';
    } else if (ch == '[') {
      inClass = true;
    } else if (ch == ']') {
      inClass = false;
    } else if (lineTerm(ch)) {
      out += ch == '\n' ? u"\\n" : ch == '\r' ? u"\\r" : ch == 0x2028 ? u"\\u2028" : u"\\u2029";
      continue;
    }
    out += c[i];
  }
  return out;
}

// new RegExp: the compilation is charged to the budget and its memory checked against the limit
RegExpObj* compileNew(VM& vm, StrObj* src, const std::string& flags) {
  std::string err;
  re::Limits lim = limits(vm);
  vm.checkTemp((size_t)src->len * 4);  // the UTF-16 copy and the parser's position data
  re::ProgramPtr p = re::compile(strU16(src), flags, err, &lim);
  if (!p) vm.error("SyntaxError: %s", err.c_str());
  uint64_t bytes = re::programSize(*p);
  return vm.newRegExp(std::shared_ptr<const re::Program>(std::move(p)), src, bytes, true);
}

// GetSubstitution: a replacement string ($$, $&, $`, $', $n, $nn, $<name>) parsed once per call into
// literal pieces and references, then applied to each match (linear in its length and the output)
struct Template {
  enum Kind : uint8_t { kLit, kGroup, kBefore, kAfter };
  struct Part {
    Kind kind;
    uint32_t a, b;  // kLit: rep[a, b); kGroup: group a
  };
  std::vector<Part> parts;
};

Template parseTemplate(VM& vm, const StrObj* rep, const re::Program& p) {
  vm.chargeWork(rep->len / 4 + 1);
  const char16_t* r = rep->chars();
  uint32_t n = rep->len;
  uint32_t m = re::captureCount(p);
  const auto& names = re::namedGroups(p);
  std::unordered_map<std::u16string, uint32_t> byName;  // built at the first $<
  Template t;
  uint32_t lit = 0;  // start of the pending literal
  auto flush = [&](uint32_t end) {
    if (end > lit) t.parts.push_back({Template::kLit, lit, end});
  };
  auto ref = [&](Template::Kind k, uint32_t group, uint32_t i, uint32_t len) {
    flush(i);
    t.parts.push_back({k, group, 0});
    lit = i + len;
  };
  uint32_t gt = 0;  // the next '>' at or after the current position (n: none to the end)
  for (uint32_t i = 0; i + 1 < n; i++) {
    if (r[i] != '$') continue;
    char16_t d = r[i + 1];
    if (d == '$') {
      flush(i + 1);  // the first '$' stays, the second is dropped
      lit = i + 2;
      i++;
    } else if (d == '&') {
      ref(Template::kGroup, 0, i, 2);
      i++;
    } else if (d == '`') {
      ref(Template::kBefore, 0, i, 2);
      i++;
    } else if (d == '\'') {
      ref(Template::kAfter, 0, i, 2);
      i++;
    } else if (d >= '0' && d <= '9') {
      uint32_t one = d - '0';
      bool two = i + 2 < n && r[i + 2] >= '0' && r[i + 2] <= '9';
      uint32_t both = two ? one * 10 + (r[i + 2] - '0') : 0;
      if (two && both >= 1 && both <= m) {
        ref(Template::kGroup, both, i, 3);
        i += 2;
      } else if (one >= 1 && one <= m) {
        ref(Template::kGroup, one, i, 2);
        i++;
      }
    } else if (d == '<' && !names.empty()) {
      if (gt < i + 2) {
        gt = i + 2;
        while (gt < n && r[gt] != '>') gt++;
      }
      if (gt >= n) continue;  // no '>' to the end: a literal "$<"
      if (byName.empty()) {
        vm.chargeWork(names.size() * 4);
        vm.checkTemp(names.size() * 64);
        for (auto& g : names) byName.emplace(utf8ToUtf16(g.name), g.index);
      }
      auto it = byName.find(std::u16string(r + i + 2, r + gt));
      if (it != byName.end()) ref(Template::kGroup, it->second, i, gt + 1 - i);
      else {
        flush(i);
        lit = gt + 1;  // an unknown name: nothing
      }
      i = gt;
    }
  }
  flush(n);
  vm.checkTemp(t.parts.size() * sizeof(Template::Part));
  return t;
}

void substitute(Builder& out, const StrObj* s, const std::vector<int32_t>& caps, const StrObj* rep, const Template& t) {
  out.vm.chargeWork(t.parts.size() / 4 + 1);
  for (const Template::Part& part : t.parts) {
    switch (part.kind) {
      case Template::kLit:
        out.append(rep->chars() + part.a, part.b - part.a);
        break;
      case Template::kGroup:
        if (caps[2 * part.a] >= 0)
          out.append(s->chars() + caps[2 * part.a], (size_t)(caps[2 * part.a + 1] - caps[2 * part.a]));
        break;
      case Template::kBefore:
        out.append(s->chars(), (size_t)caps[0]);
        break;
      case Template::kAfter:
        out.append(s->chars() + caps[1], (size_t)(s->len - (uint32_t)caps[1]));
        break;
    }
  }
}

// the matches @@replace works on: all of them (g, from 0) or the one RegExpExec gives, to `each`
template <class F>
void replaceMatches(VM& vm, RegExpObj* re, const StrObj* s, F each) {
  std::vector<int32_t> caps;
  if (!flag(re, 'g')) {
    if (builtinExec(vm, re, s, caps)) each(caps);
    return;
  }
  bool unicode = flag(re, 'u');
  re->lastIndex = 0;
  while (builtinExec(vm, re, s, caps)) {
    each(caps);
    if (caps[0] == caps[1]) re->lastIndex = advance(s, toLength(re->lastIndex), unicode);
  }
}

}  // namespace

// ---- RegExp objects

// a literal: a new object for each evaluation, sharing the program compiled when the module was loaded
E7_NATIVE(re_lit) {
  E7_UNUSED;
  ModuleRT* mod = currentModule(vm);
  int64_t i = a[0].i;
  if (!mod || i < 0 || (uint64_t)i >= mod->regexps.size()) vm.error("invalid regular expression index");
  StrObj* src = vm.newStringUtf8(mod->m.regexps[(size_t)i].source);
  r->r = vm.newRegExp(mod->regexps[(size_t)i], src, 0, false);  // counted by the module
}
// new RegExp(source, flags)
E7_NATIVE(re_new) {
  E7_UNUSED;
  r->r = compileNew(vm, S(vm, a[0]), strUtf8(S(vm, a[1])));
}
// new RegExp(re): the same pattern and flags
E7_NATIVE(re_copy) {
  E7_UNUSED;
  RegExpObj* re = Re(vm, a[0]);
  r->r = vm.newRegExp(re->prog, static_cast<StrObj*>(re->source), re->progBytes, false);
}
// new RegExp(re, flags)
E7_NATIVE(re_newRe) {
  E7_UNUSED;
  r->r = compileNew(vm, static_cast<StrObj*>(Re(vm, a[0])->source), strUtf8(S(vm, a[1])));
}
E7_NATIVE(re_exec) {
  E7_UNUSED;
  RegExpObj* re = Re(vm, a[0]);
  StrObj* s = S(vm, a[1]);
  std::vector<int32_t> caps;
  r->r = builtinExec(vm, re, s, caps) ? matchArray(vm, re, s, caps) : nullptr;
}
E7_NATIVE(re_test) {
  E7_UNUSED;
  std::vector<int32_t> caps;
  r->i = builtinExec(vm, Re(vm, a[0]), S(vm, a[1]), caps);
}
E7_NATIVE(re_lastIndex) {
  E7_UNUSED;
  r->f = Re(vm, a[0])->lastIndex;
}
E7_NATIVE(re_setLastIndex) {
  E7_UNUSED;
  Re(vm, a[0])->lastIndex = a[1].f;
}
E7_NATIVE(re_source) {
  E7_UNUSED;
  r->r = vm.newString(escapeSource(static_cast<StrObj*>(Re(vm, a[0])->source)));
}
E7_NATIVE(re_flags) {
  E7_UNUSED;
  r->r = vm.newStringAscii(re::flagText(*Re(vm, a[0])->prog).c_str());
}
// global / ignoreCase / ...: the flag letter is the registration's user data
E7_NATIVE(re_flag) {
  E7_UNUSED;
  r->i = flag(Re(vm, a[0]), (char)(uintptr_t)ud);
}
std::u16string regexpToString(const RegExpObj* re) {
  std::u16string t = u"/" + escapeSource(static_cast<const StrObj*>(re->source)) + u"/";
  for (char c : re::flagText(*re->prog)) t += (char16_t)c;
  return t;
}
E7_NATIVE(re_toString) {
  E7_UNUSED;
  r->r = vm.newString(regexpToString(Re(vm, a[0])));
}

// ---- match arrays: index / input / groups / indices (undefined on a plain array, e.g. of a g match)
static const MatchExtra* extraOf(VM& vm, Slot s) {
  ArrObj* arr = A(vm, s);
  return arr->flags & kArrFlagMatch ? matchExtra(arr) : nullptr;
}
E7_NATIVE(m_index) {
  E7_UNUSED;
  const MatchExtra* x = extraOf(vm, a[0]);
  r->f = x ? x->index : std::nan("");
}
E7_NATIVE(m_indexOpt) {
  E7_UNUSED;
  const MatchExtra* x = extraOf(vm, a[0]);
  r->r = x ? static_cast<Obj*>(vm.newBox(VT_NUMBER, slotF(x->index))) : vm.undef();
}
E7_NATIVE(m_input) {
  E7_UNUSED;
  const MatchExtra* x = extraOf(vm, a[0]);
  r->r = x ? x->input : vm.undef();
}
E7_NATIVE(m_groups) {
  E7_UNUSED;
  const MatchExtra* x = extraOf(vm, a[0]);
  r->r = x ? x->groups : vm.undef();
}
E7_NATIVE(m_indices) {
  E7_UNUSED;
  const MatchExtra* x = extraOf(vm, a[0]);
  r->r = x && x->indices ? x->indices : vm.undef();
}

// ---- String methods with a RegExp argument

// s.match(re): RegExpExec without g; with g all the matched texts (null if none)
E7_NATIVE(s_match) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  RegExpObj* re = Re(vm, a[1]);
  std::vector<int32_t> caps;
  if (!flag(re, 'g')) {
    r->r = builtinExec(vm, re, s, caps) ? matchArray(vm, re, s, caps) : nullptr;
    return;
  }
  bool unicode = flag(re, 'u');
  re->lastIndex = 0;
  ArrObj* out = nullptr;
  while (builtinExec(vm, re, s, caps)) {
    if (!out) out = vm.newArray(VT_REF, 4);
    vm.arrayPush(out, slotR(substr(vm, s, caps[0], caps[1])));
    if (caps[0] == caps[1]) re->lastIndex = advance(s, toLength(re->lastIndex), unicode);
  }
  r->r = out;
}

// s.matchAll(re): an iterator over the exec results of a copy of re (computed at once: the string is
// immutable and the copy is not observable, so the results are the same as JavaScript's lazy iterator)
E7_NATIVE(s_matchAll) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  RegExpObj* re = Re(vm, a[1]);
  if (!flag(re, 'g')) vm.error("TypeError: String.prototype.matchAll called with a non-global RegExp argument");
  RegExpObj* copy = vm.newRegExp(re->prog, static_cast<StrObj*>(re->source), re->progBytes, false);
  copy->lastIndex = toLength(re->lastIndex);
  bool unicode = flag(re, 'u');
  ArrObj* items = vm.newArray(VT_REF, 4);
  std::vector<int32_t> caps;
  while (builtinExec(vm, copy, s, caps)) {
    vm.arrayPush(items, slotR(matchArray(vm, copy, s, caps)));
    if (caps[0] == caps[1]) copy->lastIndex = advance(s, toLength(copy->lastIndex), unicode);
  }
  r->r = vm.newNativeIterator(items);
}

static void replaceWith(VM& vm, Slot* a, Slot* r, bool all) {
  StrObj* s = S(vm, a[0]);
  RegExpObj* re = Re(vm, a[1]);
  StrObj* rep = S(vm, a[2]);
  if (all && !flag(re, 'g')) vm.error("TypeError: String.prototype.replaceAll called with a non-global RegExp argument");
  Template t = parseTemplate(vm, rep, *re->prog);
  // nothing can observe the matches between them: each is substituted as it is found
  Builder out(vm);
  int32_t next = 0;
  replaceMatches(vm, re, s, [&](const std::vector<int32_t>& caps) {
    if (caps[0] < next) return;
    out.append(s->chars() + next, (size_t)(caps[0] - next));
    substitute(out, s, caps, rep, t);
    next = caps[1];
  });
  out.append(s->chars() + next, (size_t)((int32_t)s->len - next));
  r->r = vm.newString(out.out);
}
E7_NATIVE(s_replace) {
  E7_UNUSED;
  replaceWith(vm, a, r, false);
}
E7_NATIVE(s_replaceAll) {
  E7_UNUSED;
  replaceWith(vm, a, r, true);
}

// s.replace(re, fn) / s.replaceAll(re, fn): fn(match, p1, ..., pn, offset, input[, groups]), given as many
// of these arguments as it declares; the matches are all found before the first call
E7_NATIVE(s_replaceFn) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  RegExpObj* re = Re(vm, a[1]);
  CloObj* fn = Clo(vm, a[2]);
  if (a[3].i && !flag(re, 'g')) vm.error("TypeError: String.prototype.replaceAll called with a non-global RegExp argument");
  const re::Program& p = *re->prog;
  uint32_t ncap = re::captureCount(p);
  bool groups = !re::namedGroups(p).empty();
  std::string want;
  want.append(1 + ncap, 'R');
  want += "FR";
  if (groups) want += 'R';
  uint32_t np = fn->fn->nparams;
  const std::string& sig = vm.sigString(fn->fn->sigId);
  if (np > want.size() || sig != want.substr(0, np) + ">R") vm.error("the replacement function has the wrong signature");
  // all the matches first (the callbacks run after them): their captures count against the memory limit
  std::vector<int32_t> results;
  size_t width = 2 * ((size_t)ncap + 1);
  replaceMatches(vm, re, s, [&](const std::vector<int32_t>& caps) {
    vm.checkTemp((results.size() + width) * sizeof(int32_t) * 2);
    vm.chargeWork(width / 16 + 1);
    results.insert(results.end(), caps.begin(), caps.end());
  });
  Builder out(vm);
  int32_t next = 0;
  std::vector<Slot> args(np);
  std::vector<int32_t> caps(width);
  for (size_t at = 0; at < results.size(); at += width) {
    caps.assign(results.begin() + (std::ptrdiff_t)at, results.begin() + (std::ptrdiff_t)(at + width));
    for (uint32_t k = 0; k < np; k++) {
      if (k <= ncap) args[k].r = substr(vm, s, caps[2 * k], caps[2 * k + 1]);
      else if (k == ncap + 1) args[k].f = caps[0];
      else if (k == ncap + 2) args[k].r = s;
      else args[k].r = groupsObject(vm, p, caps, s, false);
    }
    // (no collection before the call copies the arguments into the callee's registers: natives block it)
    Slot v = vm.callClosure(fn, args.data(), np);
    StrObj* piece = S(vm, v);
    if (caps[0] < next) continue;
    out.append(s->chars() + next, (size_t)(caps[0] - next));
    out.append(piece->chars(), piece->len);
    next = caps[1];
  }
  out.append(s->chars() + next, (size_t)((int32_t)s->len - next));
  r->r = vm.newString(out.out);
}

// s.split(re, limit): @@split with a sticky copy of re
E7_NATIVE(s_split) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  RegExpObj* re = Re(vm, a[1]);
  double ld = a[2].f;
  uint32_t lim = 0;  // ToUint32
  if (std::isfinite(ld)) {
    double t = std::fmod(std::trunc(ld), 4294967296.0);
    if (t < 0) t += 4294967296.0;
    lim = (uint32_t)t;
  }
  ArrObj* out = vm.newArray(VT_REF, 4);
  r->r = out;
  if (lim == 0) return;
  const re::Program& p = *re->prog;
  bool unicode = re::hasFlag(p, 'u');
  re::Limits limObj = limits(vm);
  std::vector<int32_t> caps;
  uint32_t size = s->len;
  if (size == 0) {
    if (!re::exec(p, s->chars(), 0, 0, true, caps, limObj)) vm.arrayPush(out, slotR(s));
    return;
  }
  uint32_t pos = 0, q = 0;
  while (q < size) {
    if (!re::exec(p, s->chars(), size, q, true, caps, limObj) || caps[0] != (int32_t)q) {
      // (a match inside a surrogate pair starts before q: as no match at q)
      q = (uint32_t)advance(s, q, unicode);
      continue;
    }
    uint32_t e = (uint32_t)caps[1] < size ? (uint32_t)caps[1] : size;
    if (e == pos) {
      q = (uint32_t)advance(s, q, unicode);
      continue;
    }
    vm.arrayPush(out, slotR(vm.newString(s->chars() + pos, q - pos)));
    if (out->len == lim) return;
    pos = e;
    for (size_t k = 1; k < caps.size() / 2; k++) {
      vm.arrayPush(out, slotR(substr(vm, s, caps[2 * k], caps[2 * k + 1])));
      if (out->len == lim) return;
    }
    q = pos;
  }
  vm.arrayPush(out, slotR(vm.newString(s->chars() + pos, size - pos)));
}

// s.search(re): the index of the first match (lastIndex is left as it was)
E7_NATIVE(s_search) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  RegExpObj* re = Re(vm, a[1]);
  double prev = re->lastIndex;
  re->lastIndex = 0;
  std::vector<int32_t> caps;
  bool ok = builtinExec(vm, re, s, caps);
  re->lastIndex = prev;
  r->f = ok ? caps[0] : -1;
}

void registerRegExpNatives(VM& vm) {
  vm.registerNative("RegExp.lit", "I>R", re_lit);
  vm.registerNative("RegExp.new", "RR>R", re_new);
  vm.registerNative("RegExp.copy", "R>R", re_copy);
  vm.registerNative("RegExp.newRe", "RR>R", re_newRe);
  vm.registerNative("RegExp.exec", "RR>R", re_exec);
  vm.registerNative("RegExp.test", "RR>I", re_test);
  vm.registerNative("RegExp.lastIndex", "R>F", re_lastIndex);
  vm.registerNative("RegExp.setLastIndex", "RF>V", re_setLastIndex);
  vm.registerNative("RegExp.source", "R>R", re_source);
  vm.registerNative("RegExp.flags", "R>R", re_flags);
  static const std::pair<const char*, char> kFlags[] = {{"global", 'g'}, {"ignoreCase", 'i'}, {"multiline", 'm'},
                                                        {"sticky", 'y'}, {"unicode", 'u'}, {"dotAll", 's'},
                                                        {"hasIndices", 'd'}};
  for (auto& f : kFlags) vm.registerNative(std::string("RegExp.") + f.first, "R>I", re_flag, (void*)(uintptr_t)f.second);
  vm.registerNative("RegExp.toString", "R>R", re_toString);
  vm.registerNative("Match.index", "R>F", m_index);
  vm.registerNative("Match.indexOpt", "R>R", m_indexOpt);
  vm.registerNative("Match.input", "R>R", m_input);
  vm.registerNative("Match.groups", "R>R", m_groups);
  vm.registerNative("Match.indices", "R>R", m_indices);
  vm.registerNative("String.matchRe", "RR>R", s_match);
  vm.registerNative("String.matchAllRe", "RR>R", s_matchAll);
  vm.registerNative("String.replaceRe", "RRR>R", s_replace);
  vm.registerNative("String.replaceAllRe", "RRR>R", s_replaceAll);
  vm.registerNative("String.replaceReFn", "RRRI>R", s_replaceFn);
  vm.registerNative("String.splitRe", "RRF>R", s_split);
  vm.registerNative("String.searchRe", "RR>F", s_search);
  // the matcher charges its steps itself; building the results is linear in their size
  for (const char* nm : {"RegExp.exec", "RegExp.source", "RegExp.toString", "String.matchRe", "String.matchAllRe",
                         "String.replaceRe", "String.replaceAllRe", "String.replaceReFn", "String.splitRe"})
    vm.setNativeCost(nm, NC_OUT);
}

}  // namespace e7
