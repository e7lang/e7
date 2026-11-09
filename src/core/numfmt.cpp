#include "numfmt.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <cstring>

// parseInt's chunked accumulation (v * multiplier + part) must round twice, as JS arithmetic does:
// no fused multiply-add, whatever the compiler's default (CMake also passes -ffp-contract=off).
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace e7 {

// The C library's number conversions follow LC_NUMERIC; hosts may call setlocale() (e.g. a locale whose
// decimal separator is ','). Adapt to the current decimal point so results are always JS-compatible.
static std::string decimalPoint() {
  const struct lconv* lc = localeconv();
  const char* dp = lc ? lc->decimal_point : nullptr;
  return (dp && *dp) ? std::string(dp) : std::string(".");
}

double parseDoubleC(const std::string& s) {
  std::string dp = decimalPoint();
  if (dp == ".") return strtod(s.c_str(), nullptr);
  std::string t;
  t.reserve(s.size() + 4);
  for (char c : s) {
    if (c == '.') t += dp;
    else t += c;
  }
  return strtod(t.c_str(), nullptr);
}

std::string formatDoubleC(const char* fmt, int prec, double v) {
  std::string buf(1200, '\0');
  int n = snprintf(&buf[0], buf.size(), fmt, prec, v);
  buf.resize(n > 0 ? (size_t)n : 0);
  std::string dp = decimalPoint();
  if (dp != ".") {
    size_t p = buf.find(dp);
    if (p != std::string::npos) buf.replace(p, dp.size(), ".");
  }
  return buf;
}

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
// The shortest length p is the first one for which some p-digit decimal lies in the rounding interval of v.
// That decimal is the correctly rounded one (printf) or, when the interval is asymmetric (v a power of
// two), its neighbour one unit away: 2^122 is "5.316911983139664e+36" although "%.15e" gives ...663e+36.
// Among candidates the closest to v wins, as in V8.
static void shortestDigits(double v, std::string& digits, int& n) {
  uint64_t mant = 0;
  int e = 0;
  for (int p = 1; p <= 17; p++) {
    std::string sbuf = formatDoubleC("%.*e", p - 1, v);  // "d.ddde+XX", correctly rounded
    uint64_t m = 0;
    const char* c = sbuf.c_str();
    while (*c && *c != 'e' && *c != 'E') {
      if (*c >= '0' && *c <= '9') m = m * 10 + (uint64_t)(*c - '0');
      c++;
    }
    int ex = *c ? atoi(c + 1) : 0;
    mant = m;
    e = ex;
    if (p == 17) break;
    uint64_t pow10 = 1;
    for (int i = 1; i < p; i++) pow10 *= 10;  // 10^(p-1): the smallest p-digit mantissa
    auto roundTrips = [&](uint64_t cm, int cex) {
      char b[48];
      snprintf(b, sizeof b, "%llue%d", (unsigned long long)cm, cex - (p - 1));
      return parseDoubleC(b) == v;
    };
    if (roundTrips(m, ex)) break;
    uint64_t up = m + 1;
    int upEx = ex;
    if (up == pow10 * 10) { up = pow10; upEx++; }
    uint64_t down = m - 1;
    int downEx = ex;
    if (down < pow10) { down = pow10 * 10 - 1; downEx--; }
    if (roundTrips(up, upEx)) { mant = up; e = upEx; break; }
    if (roundTrips(down, downEx)) { mant = down; e = downEx; break; }
  }
  digits = std::to_string((unsigned long long)mant);
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
    return out + uint64ToString((uint64_t)d);
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

// The algorithm of V8's DoubleToRadixCString (src/numbers/conversions.cc), so that the digits match
// Node.js: fraction digits only up to the precision of the input, rounded half to even with the carry
// propagated into earlier digits (and into the integer part); integer digits below the precision of the
// double (where integer / radix >= 2^53) are zeros.
std::string numberToStringRadix(double d, int radix) {
  if (radix == 10) return numberToString(d);
  if (d != d) return "NaN";
  if (std::isinf(d)) return d < 0 ? "-Infinity" : "Infinity";
  static const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  bool neg = d < 0;
  if (neg) d = -d;
  double integer = std::floor(d);
  double fraction = d - integer;
  double delta = 0.5 * (std::nextafter(d, INFINITY) - d);
  double minDelta = std::nextafter(0.0, 1.0);
  if (!(delta > minDelta)) delta = minDelta;
  std::string frac;  // "." followed by the fraction digits
  if (fraction >= delta) {
    frac += '.';
    do {
      fraction *= radix;
      delta *= radix;
      int digit = (int)fraction;
      frac += chars[digit];
      fraction -= digit;
      if (fraction > 0.5 || (fraction == 0.5 && (digit & 1))) {
        if (fraction + delta > 1) {
          // round up: back trace the digits already written in case of a carry
          while (true) {
            if (frac.size() == 1) {  // only the '.' is left: carry into the integer part
              frac.clear();
              integer += 1;
              break;
            }
            char c = frac.back();
            int dg = c > '9' ? (c - 'a' + 10) : (c - '0');
            if (dg + 1 < radix) {
              frac.back() = chars[dg + 1];
              break;
            }
            frac.pop_back();
          }
          break;
        }
      }
    } while (fraction >= delta);
  }
  std::string ip;  // integer digits, least significant first
  while (integer / radix >= 9007199254740992.0) {  // 2^53: the remaining low digits are not represented
    integer /= radix;
    ip += '0';
  }
  do {
    double rem = std::fmod(integer, (double)radix);
    ip += chars[(int)rem];
    integer = (integer - rem) / radix;
  } while (integer > 0);
  std::string out = neg ? "-" : "";
  out.append(ip.rbegin(), ip.rend());
  return out + frac;
}

// Exact decimal expansion of |d| using printf("%.Nf") (exact in glibc, Apple, bionic, UCRT).
static std::string exactFixed(double d, int frac) { return formatDoubleC("%.*f", frac, d); }

// Round a decimal digit string (integer part "ip", fraction "fp") to f fraction digits, half-up.
std::string numberToFixed(double x, int f) {
  if (x != x) return "NaN";
  if (std::fabs(x) >= 1e21) return numberToString(x);
  if (x == 0) x = 0.0;  // -0 is not negative here: (-0).toFixed(2) === "0.00"
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
  double v = parseDoubleC(num);
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
  auto digitAt = [&](size_t k) {
    char c = digits[k];
    return (c >= '0' && c <= '9') ? c - '0' : (tolower((unsigned char)c) - 'a' + 10);
  };
  // Beyond 2^53 the result depends on the algorithm; these are V8's (NumberParseIntHelper in
  // src/numbers/conversions.cc), so that large values match Node.js.
  double v;
  if (radix == 10) {
    v = parseDoubleC(digits);  // correctly rounded
  } else if ((radix & (radix - 1)) == 0) {
    // powers of two: exact, rounded half to even (InternalStringToIntDouble)
    int log2r = 0;
    while ((1 << log2r) < radix) log2r++;
    size_t k = 0, n = digits.size();
    while (k < n && digits[k] == '0') k++;
    int64_t number = 0;
    int exponent = 0;
    for (; k < n; k++) {
      number = number * radix + digitAt(k);
      int overflow = (int)(number >> 53);
      if (overflow != 0) {
        int bits = 1;
        while (overflow > 1) { bits++; overflow >>= 1; }
        int dropped = (int)number & ((1 << bits) - 1);
        number >>= bits;
        exponent = bits;
        bool zeroTail = true;
        for (k++; k < n; k++) {
          zeroTail = zeroTail && digits[k] == '0';
          if (exponent < 2048) exponent += log2r;  // saturates: 2^2048 is Infinity already (no int overflow)
        }
        int middle = 1 << (bits - 1);
        if (dropped > middle || (dropped == middle && ((number & 1) != 0 || !zeroTail))) number++;
        if ((number & ((int64_t)1 << 53)) != 0) { exponent++; number >>= 1; }
        break;
      }
    }
    v = std::ldexp((double)number, exponent);
  } else {
    // other radices: 32-bit chunks combined in doubles (rounding errors accumulate as in V8)
    const uint32_t kMaximumMultiplier = 0xFFFFFFFFu / 36;
    size_t k = 0, n = digits.size();
    while (k < n && digits[k] == '0') k++;  // leading zeros do not start a chunk
    bool done = k == n;
    v = 0;
    while (!done) {
      uint32_t part = 0, multiplier = 1;
      while (true) {
        uint32_t m = multiplier * (uint32_t)radix;
        if (m > kMaximumMultiplier) break;
        part = part * (uint32_t)radix + (uint32_t)digitAt(k);
        multiplier = m;
        if (++k == n) { done = true; break; }
      }
      // (fused: one rounding, as V8 computes result * multiplier + part where the C++ compiler fuses it,
      // e.g. Node.js on arm64; the same on every platform here)
      v = std::fma(v, (double)multiplier, (double)part);
    }
  }
  return neg ? -v : v;
}

}  // namespace e7
