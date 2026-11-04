// Heap object layouts.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/common.h"

namespace e7 {

enum class OT : uint8_t {
  String, Array, Bytes, Buffer, Instance, Closure, Cell, Box, Map, BigInt, Undefined, Handle
};

struct Obj {
  Obj* gcNext;
  OT type;
  uint8_t marked;
  uint16_t flags;
  uint32_t size;  // bytes of this block (for accounting)
};

struct StrObj : Obj {
  uint32_t len;
  uint32_t hash;  // 0 = not computed
  char16_t* chars() { return reinterpret_cast<char16_t*>(this + 1); }
  const char16_t* chars() const { return reinterpret_cast<const char16_t*>(this + 1); }
};

struct ArrObj : Obj {
  uint8_t vt;
  uint32_t len;
  uint32_t cap;
  Slot* data;
};

struct BufObj : Obj {
  uint32_t len;
  uint8_t* data;
};

// Uint8Array / DataView view over a buffer.
struct BytesObj : Obj {
  BufObj* buf;
  uint32_t off;
  uint32_t len;
  uint8_t* ptr() { return buf->data + off; }
};

struct ClassRT;
struct FuncRT;

struct InstObj : Obj {
  ClassRT* cls;
  Slot* fields() { return reinterpret_cast<Slot*>(this + 1); }
};

struct CellObj : Obj {
  uint8_t vt;
  Slot v;
};

struct BoxObj : Obj {
  uint8_t vt;
  Slot v;
};

struct CloObj : Obj {
  FuncRT* fn;
  uint32_t nup;
  CellObj** ups() { return reinterpret_cast<CellObj**>(this + 1); }
};


struct MapEntry {
  Slot k;
  Slot v;
  uint32_t hash;
  int32_t next;  // chain
  bool live;
};

struct MapObj : Obj {
  uint8_t kvt, vvt;
  KeyMode km;
  bool isSet;
  uint32_t count;
  std::vector<MapEntry> entries;
  std::vector<int32_t> buckets;
};

struct BigObj : Obj {
  bool neg;
  std::vector<uint32_t> mag;  // little-endian limbs, no leading zeros (empty = 0)
};

struct HandleObj : Obj {
  int64_t id;
};

// Runtime class info
struct ClassRT {
  std::string name;
  ClassRT* parent = nullptr;
  bool isRecord = false;
  bool isTuple = false;
  uint32_t nfields = 0;
  std::vector<uint8_t> fieldVT;
  std::vector<std::string> fieldNames;
  std::vector<uint16_t> refFields;
  std::vector<FuncRT*> vtable;
  std::vector<std::string> methodNames;
  std::unordered_map<std::string, uint32_t> fieldIndex;
  std::unordered_map<std::string, uint32_t> methodIndex;
};

}  // namespace e7
