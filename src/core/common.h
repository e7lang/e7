#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace e7 {

// Register / storage kinds as seen by the VM.
enum Kind : uint8_t { K_I = 0, K_F = 1, K_R = 2, K_V = 3 /* void (returns only) */ };

// Precise value types used for runtime type information
// (array elements, fields, cells, boxes, map keys/values, formatting).
enum VT : uint8_t {
  VT_NUMBER = 0, VT_BOOL, VT_I8, VT_U8, VT_I16, VT_U16, VT_I32, VT_U32, VT_I64, VT_U64, VT_REF,
  VT__COUNT
};

// Map key comparison modes
enum KeyMode : uint8_t { KM_NUM, KM_INT, KM_STR, KM_BIG, KM_REF };

inline Kind kindOfVT(uint8_t vt) {
  return vt == VT_NUMBER ? K_F : vt == VT_REF ? K_R : K_I;
}
inline char kindChar(Kind k) { return "IFRV"[k]; }

struct Obj;
union Slot {
  int64_t i;
  uint64_t u;
  double f;
  Obj* r;
};
static_assert(sizeof(Slot) == 8, "Slot must be 8 bytes");

#if defined(__GNUC__) || defined(__clang__)
#define E7_LIKELY(x) __builtin_expect(!!(x), 1)
#define E7_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define E7_NOINLINE __attribute__((noinline))
#else
#define E7_LIKELY(x) (x)
#define E7_UNLIKELY(x) (x)
#define E7_NOINLINE __declspec(noinline)
#endif

// UTF helpers (implemented in core/utf.cpp)
std::u16string utf8ToUtf16(const std::string& s);
std::u16string utf8ToUtf16(const char* s, size_t n);
std::string utf16ToUtf8(const char16_t* s, size_t n);
inline std::string utf16ToUtf8(const std::u16string& s) { return utf16ToUtf8(s.data(), s.size()); }

}  // namespace e7
