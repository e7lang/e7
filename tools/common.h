#pragma once
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static inline bool readAll(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}
static inline bool writeAll(const std::string& path, const void* data, size_t n) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, n, f) == n;
  return fclose(f) == 0 && ok;
}
static inline bool parseHex(const std::string& s0, std::vector<uint8_t>& out) {
  std::string s;
  for (char c : s0)
    if (!isspace((unsigned char)c)) s += c;
  if (s.size() % 2) return false;
  out.clear();
  for (size_t i = 0; i < s.size(); i += 2) {
    unsigned v;
    if (sscanf(s.substr(i, 2).c_str(), "%2x", &v) != 1) return false;
    out.push_back((uint8_t)v);
  }
  return true;
}
static inline std::string toHex(const uint8_t* p, size_t n) {
  static const char* h = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; i++) { s += h[p[i] >> 4]; s += h[p[i] & 15]; }
  return s;
}
static inline bool endsWith(const std::string& s, const std::string& suf) {
  return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
