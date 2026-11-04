#include "numfmt.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace e7 {

int64_t doubleToInt64Bits(double d) {
  if (!(d == d) || std::isinf(d)) return 0;
  d = std::trunc(d);
  if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) return (int64_t)d;
  const double two64 = 18446744073709551616.0;
  double m = std::fmod(d, two64);
  if (m < 0) m += two64;
  uint64_t u;
  if (m >= 9223372036854775808.0) u = (uint64_t)(m - 9223372036854775808.0) + 0x8000000000000000ull;
  else u = (uint64_t)m;
  return (int64_t)u;
}

// Shortest round-trip decimal digits of a positive finite double.
// Produces digits (no leading zeros) and decimal exponent n such that value = 0.d1d2... * 10^n.
static void shortestDigits(double v, std::string& digits, int& n) {
  char buf[64];
  for (int p = 1; p <= 17; p++) {
    snprintf(buf, sizeof buf, "%.*e", p - 1, v);
    // Normalize decimal point (locale independence)
    for (char* c = buf; *c; c++) if (*c == ',') *c = '.';
    double back = strtod(buf, nullptr);
    if (back == v || p == 17) break;
  }
  // buf like "d.ddde+XX"
  digits.clear();
  const char* c = buf;
  while (*c && *c != 'e' && *c != 'E') {
    if (*c >= '0' && *c <= '9') digits.push_back(*c);
    c++;
  }
  int e = 0;
  if (*c) e = atoi(c + 1);
  while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
  n = e + 1;
}

std::string numberToString(double d) {
  if (d != d) return "NaN";
  if (d == 0) return "0";
  if (std::isinf(d)) return d < 0 ? "-Infinity" : "Infinity";
  std::string out;
  if (d < 0) { out = "-"; d = -d; }
  // Fast path for integers
  if (d < 1e21 && d == std::floor(d) && d < 9007199254740992.0) {
    char b[32];
    snprintf(b, sizeof b, "%.0f", d);
    return out + b;
  }
  std::string s;
  int n;
  shortestDigits(d, s, n);
  int k = (int)s.size();
  if (k <= n && n <= 21) {
    out += s;
    out.append(n - k, '0');
  } else if (0 < n && n <= 21) {
    out += s.substr(0, n);
    out += '.';
    out += s.substr(n);
  } else if (-6 < n && n <= 0) {
    out += "0.";
    out.append(-n, '0');
    out += s;
  } else {
    int e = n - 1;
    out += s[0];
    if (k > 1) { out += '.'; out += s.substr(1); }
    out += 'e';
    out += e < 0 ? '-' : '+';
    out += std::to_string(e < 0 ? -e : e);
  }
  return out;
}

std::string int64ToString(int64_t v, int radix) {
  if (v < 0) return "-" + uint64ToString(0 - (uint64_t)v, radix);
  return uint64ToString((uint64_t)v, radix);
}
std::string uint64ToString(uint64_t v, int radix) {
  if (radix < 2 || radix > 36) radix = 10;
  char buf[70];
  int i = 69;
  buf[i] = 0;
  do {
    int dgt = (int)(v % radix);
    buf[--i] = (char)(dgt < 10 ? '0' + dgt : 'a' + dgt - 10);
    v /= radix;
  } while (v);
  return std::string(buf + i);
}

std::string numberToStringRadix(double d, int radix) {
  if (radix == 10) return numberToString(d);
  if (d != d) return "NaN";
  if (std::isinf(d)) return d < 0 ? "-Infinity" : "Infinity";
  if (d == 0) return "0";
  bool neg = d < 0;
  if (neg) d = -d;
  double ip = std::floor(d);
  double fp = d - ip;
  std::string intPart;
  if (ip == 0) intPart = "0";
  while (ip > 0) {
    double q = std::floor(ip / radix);
    int dgt = (int)(ip - q * radix);
    if (dgt < 0) dgt = 0;
    if (dgt >= radix) dgt = radix - 1;
    intPart.push_back((char)(dgt < 10 ? '0' + dgt : 'a' + dgt - 10));
    ip = q;
  }
  std::string out(intPart.rbegin(), intPart.rend());
  if (fp > 0) {
    out += '.';
    // Emit digits until the fraction is exactly represented or precision is exhausted
    // (approximation of V8's algorithm; exact for dyadic fractions in power-of-two radices).
    double delta = 0.5 * (std::nextafter(d, INFINITY) - d);
    if (delta <= 0) delta = 1e-300;
    int count = 0;
    do {
      fp *= radix;
      delta *= radix;
      int dgt = (int)std::floor(fp);
      fp -= dgt;
      if (fp > 0.5 || (fp == 0.5 && (dgt & 1))) {
        if (fp + delta > 1) {
          // round up and stop
          dgt++;
          out.push_back((char)(dgt < 10 ? '0' + dgt : 'a' + dgt - 10));
          break;
        }
      }
      out.push_back((char)(dgt < 10 ? '0' + dgt : 'a' + dgt - 10));
      count++;
    } while (fp >= delta && count < 1100);
  }
  return neg ? "-" + out : out;
}

// Exact decimal expansion of |d| using printf("%.Nf") (exact in glibc, Apple, bionic, UCRT).
static std::string exactFixed(double d, int frac) {
  std::string buf(1200, '\0');
  int n = snprintf(&buf[0], buf.size(), "%.*f", frac, d);
  buf.resize(n > 0 ? (size_t)n : 0);
  for (auto& c : buf) if (c == ',') c = '.';
  return buf;
}

// Round a decimal digit string (integer part "ip", fraction "fp") to f fraction digits, half-up.
std::string numberToFixed(double x, int f) {
  if (x != x) return "NaN";
  if (std::fabs(x) >= 1e21) return numberToString(x);
  bool neg = x < 0;
  if (neg) x = -x;
  std::string s = exactFixed(x, 1100);
  size_t dot = s.find('.');
  std::string ip = dot == std::string::npos ? s : s.substr(0, dot);
  std::string fp = dot == std::string::npos ? "" : s.substr(dot + 1);
  if ((int)fp.size() < f + 1) fp.append(f + 1 - fp.size(), '0');
  std::string digits = ip + fp.substr(0, f);
  bool roundUp = fp[f] >= '5';
  if (roundUp) {
    int i = (int)digits.size() - 1;
    while (i >= 0) {
      if (digits[i] == '9') { digits[i] = '0'; i--; }
      else { digits[i]++; break; }
    }
    if (i < 0) digits.insert(digits.begin(), '1');
  }
  size_t ilen = digits.size() - f;
  std::string out = digits.substr(0, ilen);
  // strip leading zeros of integer part (keep one)
  size_t z = 0;
  while (z + 1 < out.size() && out[z] == '0') z++;
  out = out.substr(z);
  if (f > 0) out += "." + digits.substr(ilen);
  bool allZero = true;
  for (char c : out) if (c != '0' && c != '.') allZero = false;
  if (neg && !allZero) out = "-" + out;
  else if (neg && allZero) out = "-" + out;  // JS: (-0.0001).toFixed(2) === "-0.00"
  return out;
}

// Digits of |x| rounded to p significant digits (half-up on exact expansion). Returns digits and exponent e (x ~ d.ddd * 10^e)
static void precisionDigits(double x, int p, std::string& digits, int& e) {
  std::string s = exactFixed(x, 1100);
  size_t dot = s.find('.');
  std::string ip = s.substr(0, dot);
  std::string fp = dot == std::string::npos ? "" : s.substr(dot + 1);
  std::string all = ip + fp;
  // position of first nonzero
  size_t first = all.find_first_not_of('0');
  if (first == std::string::npos) { digits = std::string(p, '0'); e = 0; return; }
  e = (int)ip.size() - 1 - (int)first;
  std::string sig = all.substr(first);
  if ((int)sig.size() < p + 1) sig.append(p + 1 - sig.size(), '0');
  digits = sig.substr(0, p);
  if (sig[p] >= '5') {
    int i = p - 1;
    while (i >= 0) {
      if (digits[i] == '9') { digits[i] = '0'; i--; }
      else { digits[i]++; break; }
    }
    if (i < 0) { digits.insert(digits.begin(), '1'); digits.pop_back(); e++; }
  }
}

std::string numberToPrecision(double x, int p) {
  if (x != x) return "NaN";
  if (std::isinf(x)) return x < 0 ? "-Infinity" : "Infinity";
  std::string sign;
  if (x < 0) { sign = "-"; x = -x; }
  std::string d;
  int e;
  if (x == 0) { d = std::string(p, '0'); e = 0; }
  else precisionDigits(x, p, d, e);
  if (e < -6 || e >= p) {
    std::string out = sign + d.substr(0, 1);
    if (p > 1) out += "." + d.substr(1);
    out += "e";
    out += e < 0 ? "-" : "+";
    out += std::to_string(e < 0 ? -e : e);
    return out;
  }
  if (e == p - 1) return sign + d;
  if (e >= 0) return sign + d.substr(0, e + 1) + "." + d.substr(e + 1);
  return sign + "0." + std::string(-(e + 1), '0') + d;
}

std::string numberToExponential(double x, int f) {
  if (x != x) return "NaN";
  if (std::isinf(x)) return x < 0 ? "-Infinity" : "Infinity";
  std::string sign;
  if (x < 0) { sign = "-"; x = -x; }
  std::string d;
  int e;
  if (f < 0) {
    if (x == 0) { d = "0"; e = 0; }
    else { int n; shortestDigits(x, d, n); e = n - 1; }
  } else if (x == 0) {
    d = std::string(f + 1, '0');
    e = 0;
  } else {
    precisionDigits(x, f + 1, d, e);
  }
  std::string out = sign + d.substr(0, 1);
  if (d.size() > 1) out += "." + d.substr(1);
  out += "e";
  out += e < 0 ? "-" : "+";
  out += std::to_string(e < 0 ? -e : e);
  return out;
}

bool isJSWhitespace(char16_t c) {
  switch (c) {
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0: case 0x1680:
    case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
      return true;
    default:
      return c >= 0x2000 && c <= 0x200A;
  }
}

static std::string asciiTrim(const std::u16string& s, bool& ok) {
  size_t b = 0, e = s.size();
  while (b < e && isJSWhitespace(s[b])) b++;
  while (e > b && isJSWhitespace(s[e - 1])) e--;
  std::string out;
  ok = true;
  for (size_t i = b; i < e; i++) {
    if (s[i] > 127) { ok = false; return out; }
    out.push_back((char)s[i]);
  }
  return out;
}

static double parseDecimalPrefix(const std::string& t, size_t& used) {
  // StrDecimalLiteral: [+-] (Infinity | digits[.digits][e[+-]digits] | .digits[e..])
  size_t i = 0;
  bool neg = false;
  if (i < t.size() && (t[i] == '+' || t[i] == '-')) { neg = t[i] == '-'; i++; }
  if (t.compare(i, 8, "Infinity") == 0) { used = i + 8; return neg ? -INFINITY : INFINITY; }
  size_t start = i;
  size_t nd = 0;
  while (i < t.size() && isdigit((unsigned char)t[i])) { i++; nd++; }
  if (i < t.size() && t[i] == '.') {
    i++;
    while (i < t.size() && isdigit((unsigned char)t[i])) { i++; nd++; }
  }
  if (nd == 0) { used = 0; return NAN; }
  if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
    size_t j = i + 1;
    if (j < t.size() && (t[j] == '+' || t[j] == '-')) j++;
    size_t ed = 0;
    while (j < t.size() && isdigit((unsigned char)t[j])) { j++; ed++; }
    if (ed) i = j;
  }
  std::string num = t.substr(start, i - start);
  used = i;
  double v = strtod(num.c_str(), nullptr);
  return neg ? -v : v;
}

double stringToNumber(const std::u16string& s) {
  bool ok;
  std::string t = asciiTrim(s, ok);
  if (!ok) return NAN;
  if (t.empty()) return 0;
  if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X' || t[1] == 'o' || t[1] == 'O' || t[1] == 'b' || t[1] == 'B')) {
    int radix = (t[1] == 'x' || t[1] == 'X') ? 16 : (t[1] == 'o' || t[1] == 'O') ? 8 : 2;
    double v = 0;
    for (size_t i = 2; i < t.size(); i++) {
      int dgt;
      char c = (char)tolower((unsigned char)t[i]);
      if (c >= '0' && c <= '9') dgt = c - '0';
      else if (c >= 'a' && c <= 'z') dgt = c - 'a' + 10;
      else return NAN;
      if (dgt >= radix) return NAN;
      v = v * radix + dgt;
    }
    return v;
  }
  size_t used;
  double v = parseDecimalPrefix(t, used);
  if (used != t.size()) return NAN;
  return v;
}

double parseFloatJS(const std::u16string& s) {
  size_t b = 0;
  while (b < s.size() && isJSWhitespace(s[b])) b++;
  std::string t;
  for (size_t i = b; i < s.size() && s[i] < 128; i++) t.push_back((char)s[i]);
  size_t used;
  double v = parseDecimalPrefix(t, used);
  if (used == 0) return NAN;
  return v;
}

double parseIntJS(const std::u16string& s, int radix) {
  size_t i = 0;
  while (i < s.size() && isJSWhitespace(s[i])) i++;
  bool neg = false;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
  bool strip = true;
  if (radix != 0) {
    if (radix < 2 || radix > 36) return NAN;
    if (radix != 16) strip = false;
  } else {
    radix = 10;
  }
  if (strip && i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
    i += 2;
    radix = 16;
  }
  size_t start = i;
  std::string digits;
  while (i < s.size()) {
    char16_t c = s[i];
    int dgt;
    if (c >= '0' && c <= '9') dgt = c - '0';
    else if (c >= 'a' && c <= 'z') dgt = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z') dgt = c - 'A' + 10;
    else break;
    if (dgt >= radix) break;
    digits.push_back((char)c);
    i++;
  }
  if (i == start) return NAN;
  double v;
  if (radix == 10) {
    v = strtod(digits.c_str(), nullptr);  // correctly rounded
  } else {
    v = 0;
    for (char c : digits) {
      int dgt = (c >= '0' && c <= '9') ? c - '0' : (tolower((unsigned char)c) - 'a' + 10);
      v = v * radix + dgt;
    }
  }
  return neg ? -v : v;
}

}  // namespace e7
