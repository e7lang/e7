#include "common.h"

namespace e7 {

std::u16string utf8ToUtf16(const char* s, size_t n) {
  std::u16string out;
  out.reserve(n);
  size_t i = 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  while (i < n) {
    uint32_t c = p[i];
    uint32_t cp;
    int extra;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { out.push_back(0xFFFD); i++; continue; }
    bool bad = false;
    for (int k = 1; k <= extra; k++) {
      if (i + k >= n || (p[i + k] & 0xC0) != 0x80) { bad = true; break; }
      cp = (cp << 6) | (p[i + k] & 0x3F);
    }
    if (bad || (extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF)) ||
        (cp >= 0xD800 && cp <= 0xDFFF)) {
      out.push_back(0xFFFD);
      i++;
      continue;
    }
    i += extra + 1;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back((char16_t)(0xD800 + (cp >> 10)));
      out.push_back((char16_t)(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back((char16_t)cp);
    }
  }
  return out;
}

std::u16string utf8ToUtf16(const std::string& s) { return utf8ToUtf16(s.data(), s.size()); }

std::string utf16ToUtf8(const char16_t* s, size_t n) {
  std::string out;
  out.reserve(n);
  for (size_t i = 0; i < n; i++) {
    uint32_t c = s[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
      i++;
    } else if (c >= 0xD800 && c <= 0xDFFF) {
      c = 0xFFFD;  // lone surrogate (same as TextEncoder)
    }
    if (c < 0x80) out.push_back((char)c);
    else if (c < 0x800) { out.push_back((char)(0xC0 | (c >> 6))); out.push_back((char)(0x80 | (c & 0x3F))); }
    else if (c < 0x10000) {
      out.push_back((char)(0xE0 | (c >> 12)));
      out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
      out.push_back((char)(0x80 | (c & 0x3F)));
    } else {
      out.push_back((char)(0xF0 | (c >> 18)));
      out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
      out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
      out.push_back((char)(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

}  // namespace e7
