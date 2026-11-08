// String natives (UTF-16 semantics, as in JavaScript).
#include <algorithm>
#include "util.h"

namespace e7 {

static StrObj* sub(VM& vm, StrObj* s, int64_t b, int64_t e) {
  if (b <= 0 && e >= s->len) return s;
  if (e <= b) return vm.newString(u"", 0);
  return vm.newString(s->chars() + b, (size_t)(e - b));
}

// Substring search in O(n + m) (Knuth-Morris-Pratt), so adversarial inputs cannot make it quadratic.
// With rev, finds the last match starting at or before `from` by searching the reversed strings.
static int64_t kmpFind(const char16_t* h, int64_t hl, const char16_t* nd, int64_t nl, int64_t from, bool rev) {
  auto H = [&](int64_t i) { return rev ? h[hl - 1 - i] : h[i]; };
  auto N = [&](int64_t j) { return rev ? nd[nl - 1 - j] : nd[j]; };
  int64_t start = rev ? hl - from - nl : from;
  if (start < 0) start = 0;
  if (nl <= 4) {  // short needles: direct comparison is O(4n)
    for (int64_t i = start; i + nl <= hl; i++) {
      int64_t j = 0;
      while (j < nl && H(i + j) == N(j)) j++;
      if (j == nl) return rev ? hl - i - nl : i;
    }
    return -1;
  }
  std::vector<int32_t> fail((size_t)nl, 0);
  for (int64_t i = 1, k = 0; i < nl; i++) {
    while (k > 0 && N(i) != N(k)) k = fail[(size_t)k - 1];
    if (N(i) == N(k)) k++;
    fail[(size_t)i] = (int32_t)k;
  }
  for (int64_t i = start, k = 0; i < hl; i++) {
    while (k > 0 && H(i) != N(k)) k = fail[(size_t)k - 1];
    if (H(i) == N(k)) k++;
    if (k == nl) {
      int64_t at = i - nl + 1;
      return rev ? hl - at - nl : at;
    }
  }
  return -1;
}
static int64_t findStr(const StrObj* h, const StrObj* nd, int64_t from) {
  int64_t hl = h->len, nl = nd->len;
  if (from < 0) from = 0;
  if (nl == 0) return from <= hl ? from : hl;
  if (from + nl > hl) return -1;
  return kmpFind(h->chars(), hl, nd->chars(), nl, from, false);
}

E7_NATIVE(s_charAt) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  double i = toInteger(a[1].f);
  if (i < 0 || i >= s->len) { r->r = vm.newString(u"", 0); return; }
  r->r = vm.newString(s->chars() + (size_t)i, 1);
}
E7_NATIVE(s_charCodeAt) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  double i = toInteger(a[1].f);
  r->f = (i < 0 || i >= s->len) ? NAN : (double)s->chars()[(size_t)i];
}
E7_NATIVE(s_codePointAt) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  double i = toInteger(a[1].f);
  if (i < 0 || i >= s->len) { r->r = vm.undef(); return; }
  size_t k = (size_t)i;
  uint32_t c = s->chars()[k];
  if (c >= 0xD800 && c <= 0xDBFF && k + 1 < s->len) {
    uint32_t d = s->chars()[k + 1];
    if (d >= 0xDC00 && d <= 0xDFFF) c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
  }
  Slot v;
  v.f = c;
  r->r = vm.newBox(VT_NUMBER, v);
}
E7_NATIVE(s_at) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  double i = toInteger(a[1].f);
  if (i < 0) i += s->len;
  if (i < 0 || i >= s->len) { r->r = vm.undef(); return; }
  r->r = vm.newString(s->chars() + (size_t)i, 1);
}
E7_NATIVE(s_index) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  double d = a[1].f;
  if (!(d >= 0 && d < s->len) || (double)(uint32_t)d != d)
    vm.error("string index %s out of range (length %u)", numberToString(d).c_str(), s->len);
  r->r = vm.newString(s->chars() + (size_t)d, 1);
}
E7_NATIVE(s_indexOf) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  StrObj* t = S(vm, a[1]);
  r->f = (double)findStr(s, t, clampIndex(a[2].f, s->len));
}
E7_NATIVE(s_lastIndexOf) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  StrObj* t = S(vm, a[1]);
  double fd = a[2].f;
  int64_t from = fd != fd ? s->len : clampIndex(fd, s->len);
  int64_t start = std::min<int64_t>(from, (int64_t)s->len - (int64_t)t->len);
  if (start < 0) { r->f = -1; return; }
  if (t->len == 0) { r->f = (double)start; return; }
  r->f = (double)kmpFind(s->chars(), s->len, t->chars(), t->len, start, true);
}
E7_NATIVE(s_includes) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  r->i = findStr(s, S(vm, a[1]), clampIndex(a[2].f, s->len)) >= 0;
}
E7_NATIVE(s_startsWith) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  StrObj* t = S(vm, a[1]);
  int64_t p = clampIndex(a[2].f, s->len);
  r->i = p + t->len <= s->len && memcmp(s->chars() + p, t->chars(), (size_t)t->len * 2) == 0;
}
E7_NATIVE(s_endsWith) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  StrObj* t = S(vm, a[1]);
  int64_t e = clampIndex(a[2].f, s->len);
  int64_t b = e - t->len;
  r->i = b >= 0 && memcmp(s->chars() + b, t->chars(), (size_t)t->len * 2) == 0;
}
E7_NATIVE(s_slice) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  r->r = sub(vm, s, relIndex(a[1].f, s->len), relIndex(a[2].f, s->len));
}
E7_NATIVE(s_substring) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  int64_t b = clampIndex(a[1].f, s->len), e = clampIndex(a[2].f, s->len);
  if (b > e) std::swap(b, e);
  r->r = sub(vm, s, b, e);
}
E7_NATIVE(s_substr) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  int64_t b = relIndex(a[1].f, s->len);
  double l = toInteger(a[2].f);
  int64_t len = l < 0 ? 0 : (l > (double)s->len ? s->len : (int64_t)l);
  int64_t e = std::min<int64_t>(b + len, s->len);
  r->r = sub(vm, s, b, e);
}

// Case mapping: the full, locale-independent Unicode mappings of JavaScript (tables generated from
// Node.js by scripts/gen-casemap.mjs), with Final_Sigma (Σ at the end of a word lowercases to ς)
namespace {
struct CaseRun { uint32_t lo; uint16_t len; int32_t delta; uint8_t stride; };
struct CaseSpecial { uint32_t cp; uint8_t n; char16_t out[3]; };
struct CaseRange { uint32_t lo, hi; };
#include "casemap.inc"

template <size_t N>
uint32_t mapRun(const CaseRun (&t)[N], uint32_t c) {
  size_t lo = 0, hi = N;
  while (lo < hi) {  // the last run starting at or before c
    size_t mid = (lo + hi) / 2;
    if (t[mid].lo <= c) lo = mid + 1;
    else hi = mid;
  }
  if (lo == 0) return c;
  const CaseRun& r = t[lo - 1];
  uint32_t off = c - r.lo;
  if (off % r.stride || off / r.stride >= r.len) return c;
  return (uint32_t)((int64_t)c + r.delta);
}
template <size_t N>
const CaseSpecial* findSpecial(const CaseSpecial (&t)[N], uint32_t c) {
  size_t lo = 0, hi = N;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (t[mid].cp < c) lo = mid + 1;
    else hi = mid;
  }
  return lo < N && t[lo].cp == c ? &t[lo] : nullptr;
}
template <size_t N>
bool inRanges(const CaseRange (&t)[N], uint32_t c) {
  size_t lo = 0, hi = N;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (t[mid].hi < c) lo = mid + 1;
    else hi = mid;
  }
  return lo < N && t[lo].lo <= c;
}

// the code point at i (a lone surrogate is itself); n: its length in code units
uint32_t cpAt(const char16_t* s, uint32_t len, uint32_t i, uint32_t& n) {
  char16_t c = s[i];
  if (c >= 0xD800 && c <= 0xDBFF && i + 1 < len && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
    n = 2;
    return 0x10000 + (((uint32_t)c - 0xD800) << 10) + ((uint32_t)s[i + 1] - 0xDC00);
  }
  n = 1;
  return c;
}
// the code point ending before i
uint32_t cpBefore(const char16_t* s, uint32_t i, uint32_t& n) {
  char16_t c = s[i - 1];
  if (c >= 0xDC00 && c <= 0xDFFF && i >= 2 && s[i - 2] >= 0xD800 && s[i - 2] <= 0xDBFF) {
    n = 2;
    return 0x10000 + (((uint32_t)s[i - 2] - 0xD800) << 10) + ((uint32_t)c - 0xDC00);
  }
  n = 1;
  return c;
}
void appendCp(std::u16string& out, uint32_t c) {
  if (c >= 0x10000) {
    c -= 0x10000;
    out += (char16_t)(0xD800 + (c >> 10));
    out += (char16_t)(0xDC00 + (c & 0x3FF));
  } else {
    out += (char16_t)c;
  }
}
// Final_Sigma: a cased letter (then case-ignorables) before, and no (case-ignorables then) cased letter after
bool finalSigma(const char16_t* s, uint32_t len, uint32_t i, uint32_t n) {
  bool before = false;
  for (uint32_t j = i; j > 0;) {
    uint32_t k;
    uint32_t c = cpBefore(s, j, k);
    j -= k;
    if (inRanges(kCaseIgnorable, c)) continue;
    before = inRanges(kCased, c);
    break;
  }
  if (!before) return false;
  for (uint32_t j = i + n; j < len;) {
    uint32_t k;
    uint32_t c = cpAt(s, len, j, k);
    j += k;
    if (inRanges(kCaseIgnorable, c)) continue;
    return !inRanges(kCased, c);
  }
  return true;
}
}  // namespace

E7_NATIVE(s_toUpper) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  const char16_t* p = s->chars();
  std::u16string out;
  out.reserve(s->len);
  for (uint32_t i = 0; i < s->len;) {
    uint32_t n;
    uint32_t c = cpAt(p, s->len, i, n);
    i += n;
    if (c < 0x80) { out += (char16_t)(c >= 'a' && c <= 'z' ? c - 32 : c); continue; }
    if (const CaseSpecial* sp = findSpecial(kUpperSpecial, c)) { out.append(sp->out, sp->n); continue; }
    appendCp(out, mapRun(kUpper, c));
  }
  vm.checkTemp(out.size() * 2);
  r->r = vm.newString(out);
}
E7_NATIVE(s_toLower) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  const char16_t* p = s->chars();
  std::u16string out;
  out.reserve(s->len);
  for (uint32_t i = 0; i < s->len;) {
    uint32_t n;
    uint32_t c = cpAt(p, s->len, i, n);
    if (c < 0x80) { out += (char16_t)(c >= 'A' && c <= 'Z' ? c + 32 : c); i += n; continue; }
    if (c == 0x3A3) { out += finalSigma(p, s->len, i, n) ? u'\u03C2' : u'\u03C3'; i += n; continue; }
    i += n;
    if (const CaseSpecial* sp = findSpecial(kLowerSpecial, c)) { out.append(sp->out, sp->n); continue; }
    appendCp(out, mapRun(kLower, c));
  }
  vm.checkTemp(out.size() * 2);
  r->r = vm.newString(out);
}
static void trimImpl(VM& vm, Slot* a, Slot* r, bool start, bool end) {
  StrObj* s = S(vm, a[0]);
  int64_t b = 0, e = s->len;
  if (start) while (b < e && isJSWhitespace(s->chars()[b])) b++;
  if (end) while (e > b && isJSWhitespace(s->chars()[e - 1])) e--;
  r->r = sub(vm, s, b, e);
}
E7_NATIVE(s_trim) { E7_UNUSED; trimImpl(vm, a, r, true, true); }
E7_NATIVE(s_trimStart) { E7_UNUSED; trimImpl(vm, a, r, true, false); }
E7_NATIVE(s_trimEnd) { E7_UNUSED; trimImpl(vm, a, r, false, true); }

static void padImpl(VM& vm, Slot* a, Slot* r, bool start) {
  StrObj* s = S(vm, a[0]);
  StrObj* f = S(vm, a[2]);
  double target = toInteger(a[1].f);
  if (target <= s->len || f->len == 0) { r->r = s; return; }
  if (target > 0x3FFFFFFF) vm.error("RangeError: invalid string length");
  size_t fill = (size_t)target - s->len;
  std::u16string pad;
  pad.reserve(fill);
  while (pad.size() < fill) pad.append(f->chars(), std::min<size_t>(f->len, fill - pad.size()));
  std::u16string out = start ? pad + strU16(s) : strU16(s) + pad;
  r->r = vm.newString(out);
}
E7_NATIVE(s_padStart) { E7_UNUSED; padImpl(vm, a, r, true); }
E7_NATIVE(s_padEnd) { E7_UNUSED; padImpl(vm, a, r, false); }
E7_NATIVE(s_repeat) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  double c = toInteger(a[1].f);
  if (c < 0 || std::isinf(c)) vm.error("RangeError: invalid count value: %s", numberToString(a[1].f).c_str());
  if (s->len == 0 || c == 0) { r->r = vm.newString(u"", 0); return; }
  if (c * s->len > 0x3FFFFFFF) vm.error("RangeError: invalid string length");
  size_t total = (size_t)c * s->len;
  StrObj* o = vm.allocString(total);
  for (size_t i = 0; i < (size_t)c; i++) memcpy(o->chars() + i * s->len, s->chars(), (size_t)s->len * 2);
  r->r = o;
}
E7_NATIVE(s_split) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  StrObj* sep = S(vm, a[1]);
  uint32_t lim = (uint32_t)doubleToInt64Bits(a[2].f);
  ArrObj* arr = vm.newArray(VT_REF, 0);
  if (lim == 0) { r->r = arr; return; }
  if (sep->len == 0) {
    for (uint32_t i = 0; i < s->len && arr->len < lim; i++) vm.arrayPush(arr, slotR(vm.newString(s->chars() + i, 1)));
    r->r = arr;
    return;
  }
  int64_t p = 0;
  while (true) {
    int64_t q = findStr(s, sep, p);
    if (q < 0) break;
    vm.arrayPush(arr, slotR(vm.newString(s->chars() + p, (size_t)(q - p))));
    if (arr->len >= lim) { r->r = arr; return; }
    p = q + sep->len;
  }
  vm.arrayPush(arr, slotR(vm.newString(s->chars() + p, (size_t)(s->len - p))));
  r->r = arr;
}

// GetSubstitution for string patterns ($$, $&, $`, $')
static void appendReplacement(VM& vm, std::u16string& out, const StrObj* s, const StrObj* rep, int64_t pos, int64_t mlen) {
  const char16_t* rc = rep->chars();
  // the result can grow quadratically ($` / $'), so bound it by the memory limit as it grows
  vm.checkTemp((out.size() + rep->len + s->len) * 2);
  for (uint32_t i = 0; i < rep->len; i++) {
    if (rc[i] == '$' && i + 1 < rep->len) {
      char16_t n = rc[i + 1];
      if (n == '$') { out += u'$'; i++; continue; }
      if (n == '&') { out.append(s->chars() + pos, (size_t)mlen); i++; continue; }
      if (n == '`') { out.append(s->chars(), (size_t)pos); i++; continue; }
      if (n == '\'') { out.append(s->chars() + pos + mlen, (size_t)(s->len - pos - mlen)); i++; continue; }
    }
    out += rc[i];
  }
}
static void replaceImpl(VM& vm, Slot* a, Slot* r, bool all) {
  StrObj* s = S(vm, a[0]);
  StrObj* pat = S(vm, a[1]);
  StrObj* rep = S(vm, a[2]);
  std::u16string out;
  if (pat->len == 0) {
    if (!all) {
      appendReplacement(vm, out, s, rep, 0, 0);
      out.append(s->chars(), s->len);
    } else {
      for (uint32_t i = 0; i <= s->len; i++) {
        appendReplacement(vm, out, s, rep, i, 0);
        if (i < s->len) out += s->chars()[i];
      }
    }
    r->r = vm.newString(out);
    return;
  }
  int64_t p = 0;
  int64_t q = findStr(s, pat, 0);
  if (q < 0) { r->r = s; return; }
  while (q >= 0) {
    out.append(s->chars() + p, (size_t)(q - p));
    appendReplacement(vm, out, s, rep, q, pat->len);
    p = q + pat->len;
    if (!all) break;
    q = findStr(s, pat, p);
  }
  out.append(s->chars() + p, (size_t)(s->len - p));
  r->r = vm.newString(out);
}
E7_NATIVE(s_replace) { E7_UNUSED; replaceImpl(vm, a, r, false); }
E7_NATIVE(s_replaceAll) { E7_UNUSED; replaceImpl(vm, a, r, true); }
E7_NATIVE(s_localeCompare) {
  E7_UNUSED;
  int c = strCmp(S(vm, a[0]), S(vm, a[1]));
  r->f = c < 0 ? -1 : c > 0 ? 1 : 0;
}
E7_NATIVE(s_fromCharCodes) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  if (arr->vt != VT_NUMBER) vm.error("expected number[]");
  StrObj* s = vm.allocString(arr->len);
  for (uint32_t i = 0; i < arr->len; i++) s->chars()[i] = (char16_t)(uint16_t)(uint64_t)doubleToInt64Bits(arr->data[i].f);
  r->r = s;
}
E7_NATIVE(s_fromCodePoints) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  if (arr->vt != VT_NUMBER) vm.error("expected number[]");
  std::u16string out;
  for (uint32_t i = 0; i < arr->len; i++) {
    double d = arr->data[i].f;
    if (!(d >= 0 && d <= 0x10FFFF) || std::trunc(d) != d) vm.error("RangeError: invalid code point %s", numberToString(d).c_str());
    uint32_t cp = (uint32_t)d;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out += (char16_t)(0xD800 + (cp >> 10));
      out += (char16_t)(0xDC00 + (cp & 0x3FF));
    } else {
      out += (char16_t)cp;
    }
  }
  r->r = vm.newString(out);
}
static uint32_t cpLen(const StrObj* s, uint32_t i) {
  char16_t c = s->chars()[i];
  if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s->len) {
    char16_t d = s->chars()[i + 1];
    if (d >= 0xDC00 && d <= 0xDFFF) return 2;
  }
  return 1;
}
E7_NATIVE(s_iterNext) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  int64_t i = a[1].i;
  if (i < 0 || i >= s->len) { r->i = -1; return; }
  r->i = i + cpLen(s, (uint32_t)i);
}
E7_NATIVE(s_codePointStr) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  int64_t i = a[1].i;
  if (i < 0 || i >= s->len) vm.error("string iterator out of range");
  r->r = vm.newString(s->chars() + i, cpLen(s, (uint32_t)i));
}
E7_NATIVE(s_chars) {
  E7_UNUSED;
  StrObj* s = S(vm, a[0]);
  ArrObj* arr = vm.newArray(VT_REF, 0);
  for (uint32_t i = 0; i < s->len;) {
    uint32_t l = cpLen(s, i);
    vm.arrayPush(arr, slotR(vm.newString(s->chars() + i, l)));
    i += l;
  }
  r->r = arr;
}

void registerStringNatives(VM& vm) {
  vm.registerNative("String.charAt", "RF>R", s_charAt);
  vm.registerNative("String.charCodeAt", "RF>F", s_charCodeAt);
  vm.registerNative("String.codePointAt", "RF>R", s_codePointAt);
  vm.registerNative("String.at", "RF>R", s_at);
  vm.registerNative("String.index", "RF>R", s_index);
  vm.registerNative("String.indexOf", "RRF>F", s_indexOf);
  vm.registerNative("String.lastIndexOf", "RRF>F", s_lastIndexOf);
  vm.registerNative("String.includes", "RRF>I", s_includes);
  vm.registerNative("String.startsWith", "RRF>I", s_startsWith);
  vm.registerNative("String.endsWith", "RRF>I", s_endsWith);
  vm.registerNative("String.slice", "RFF>R", s_slice);
  vm.registerNative("String.substring", "RFF>R", s_substring);
  vm.registerNative("String.substr", "RFF>R", s_substr);
  vm.registerNative("String.toUpperCase", "R>R", s_toUpper);
  vm.registerNative("String.toLowerCase", "R>R", s_toLower);
  vm.registerNative("String.trim", "R>R", s_trim);
  vm.registerNative("String.trimStart", "R>R", s_trimStart);
  vm.registerNative("String.trimEnd", "R>R", s_trimEnd);
  vm.registerNative("String.padStart", "RFR>R", s_padStart);
  vm.registerNative("String.padEnd", "RFR>R", s_padEnd);
  vm.registerNative("String.repeat", "RF>R", s_repeat);
  vm.registerNative("String.split", "RRF>R", s_split);
  vm.registerNative("String.replace", "RRR>R", s_replace);
  vm.registerNative("String.replaceAll", "RRR>R", s_replaceAll);
  vm.registerNative("String.localeCompare", "RR>F", s_localeCompare);
  vm.registerNative("String.fromCharCodes", "R>R", s_fromCharCodes);
  vm.registerNative("String.fromCodePoints", "R>R", s_fromCodePoints);
  vm.registerNative("String.iterNext", "RI>I", s_iterNext);
  vm.registerNative("String.codePointStr", "RI>R", s_codePointStr);
  vm.registerNative("String.chars", "R>R", s_chars);
}

}  // namespace e7
