// Heap object layouts.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/common.h"

namespace e7 {

enum class OT : uint8_t {
  String, Array, Bytes, Buffer, Instance, Closure, Cell, Box, Map, BigInt, Undefined, Handle,
  Coro,     // 0.3: a generator or async function activation (DESIGN-0.3.md D1)
  Promise,  // 0.3: a promise (D2)
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

// Obj::flags of an Array: an Error object, [name, message] (created only by the VM and the Error natives)
constexpr uint16_t kArrFlagError = 2;

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
  const Slot* fields() const { return reinterpret_cast<const Slot*>(this + 1); }
  // records with a presence mask (D7): one bit per field after the fields (set: an own property)
  inline uint64_t* mask();
  inline const uint64_t* mask() const;
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
  int64_t seq;  // insertion number: the iteration cursor (survives compaction and clear, see map.cpp)
  uint32_t hash;
  int32_t next;  // chain
  bool live;
};

struct MapObj : Obj {
  uint8_t kvt, vvt;
  KeyMode km;
  bool isSet;
  uint32_t count;
  int64_t nextSeq = 0;  // seq of the next inserted entry (never reset, also not by clear)
  std::vector<MapEntry> entries;  // in insertion (seq) order; dead entries are compacted away by rehash
  std::vector<int32_t> buckets;
};

struct BigObj : Obj {
  bool neg;
  std::vector<uint32_t> mag;  // little-endian limbs, no leading zeros (empty = 0)
};

struct HandleObj : Obj {
  int64_t id;
};

// A coroutine: the frame of a generator or async function while it is not running (DESIGN-0.3.md D1).
// While it runs, its registers are in an ordinary frame on the VM stack (Frame::coro points here) and
// the saved copy below is stale (not traced by the collector).
enum CoroState : uint8_t { CO_START, CO_SUSPENDED, CO_RUNNING, CO_DONE };
struct PromiseObj;
struct CoroObj : Obj {
  FuncRT* fn;
  CloObj* closure;
  PromiseObj* promise;  // async function: the promise it returns (null for generators)
  uint32_t pc;          // CO_START: the first instruction to run; CO_SUSPENDED: the YIELD / AWAIT it waits at
  uint32_t nregs;
  uint8_t fflags;       // the function's flags (kind and value kinds)
  uint8_t state;        // CoroState
  Slot* regs() { return reinterpret_cast<Slot*>(this + 1); }
};

// Runtime class info
struct ClassRT {
  std::string name;
  ClassRT* parent = nullptr;
  bool isRecord = false;
  bool isTuple = false;
  bool presence = false;  // instances carry a presence mask (D7, format-3 class flag)
  uint32_t nfields = 0;
  std::vector<uint8_t> fieldVT;
  std::vector<std::string> fieldNames;
  std::vector<uint16_t> refFields;
  std::vector<FuncRT*> vtable;
  std::vector<std::string> methodNames;
  std::unordered_map<std::string, uint32_t> fieldIndex;
  std::unordered_map<std::string, uint32_t> methodIndex;
};

inline uint64_t* InstObj::mask() { return reinterpret_cast<uint64_t*>(fields() + cls->nfields); }
inline const uint64_t* InstObj::mask() const { return reinterpret_cast<const uint64_t*>(fields() + cls->nfields); }
inline size_t presenceWords(uint32_t nfields) { return (nfields + 63) / 64; }

// Whether field i of an instance is an own property: class fields always are; a record field is unless
// its presence bit is clear (records with a mask) or, in records without one (0.2 modules), it holds
// undefined (a missing optional property)
inline bool fieldPresent(const InstObj* in, uint32_t i, const Obj* undef) {
  const ClassRT* c = in->cls;
  if (!c->isRecord) return true;
  if (c->presence) return (in->mask()[i / 64] >> (i % 64)) & 1;
  return !(c->fieldVT[i] == VT_REF && in->fields()[i].r == undef);
}
inline void setFieldPresent(InstObj* in, uint32_t i, bool on) {
  uint64_t bit = 1ull << (i % 64);
  if (on) in->mask()[i / 64] |= bit;
  else in->mask()[i / 64] &= ~bit;
}

}  // namespace e7
