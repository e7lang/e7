// JavaScript-compatible number <-> string conversions.
#pragma once
#include <cstdint>
#include <string>

namespace e7 {
// Number.prototype.toString() (radix 10), e.g. 0.1+0.2 -> "0.30000000000000004", 1e21 -> "1e+21"
std::string numberToString(double d);
// Number.prototype.toString(radix) for radix 2..36
std::string numberToStringRadix(double d, int radix);
// Number.prototype.toFixed(digits), digits 0..100
std::string numberToFixed(double d, int digits);
// Number.prototype.toPrecision(p), p 1..100
std::string numberToPrecision(double d, int p);
// Number.prototype.toExponential(digits); digits < 0 means "as many as needed"
std::string numberToExponential(double d, int digits);
// Number(string) semantics (StringToNumber); whitespace trimmed, "" -> 0, invalid -> NaN
double stringToNumber(const std::u16string& s);
// parseFloat / parseInt semantics
double parseFloatJS(const std::u16string& s);
double parseIntJS(const std::u16string& s, int radix);  // radix 0 = auto
// Signed/unsigned 64-bit integer formatting
std::string int64ToString(int64_t v, int radix = 10);
std::string uint64ToString(uint64_t v, int radix = 10);
// ToInt32-style modular conversion of a double to 64-bit two's complement bits.
int64_t doubleToInt64Bits(double d);
// JS whitespace (for trim)
bool isJSWhitespace(char16_t c);
}  // namespace e7
