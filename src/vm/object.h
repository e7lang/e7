// Heap object layouts.
#pragma once
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/common.h"

namespace e7 {

enum class OT : uint8_t {
  String, Array, Bytes, Buffer, Instance, Closure, Cell, Box, Map, BigInt, Undefined, Handle,
  Coro,     // a generator or async function activation (DESIGN.md D1)
  Promise,  // a promise (D2)
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

// Obj::flags of an Array: an Error object, [name, message] (created only by the VM and the Error natives).
// Bits 8-11: the error's class (index + 1) once its name was changed; bit 12: a name assigned (an own
// enumerable property); bit 13: a message assigned to an error without one.
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

// A coroutine: the frame of a generator or async function while it is not running (DESIGN.md D1).
// While it runs, its registers are in an ordinary frame on the VM stack (Frame::coro points here) and
// the saved copy below is stale (not traced by the collector).
// CO_AWAIT_RETURN (async generators): the specification's awaiting-return state (return(v) waits
// for v before it completes). An async generator suspended at an AWAIT is CO_SUSPENDED (the
// specification's executing state): the instruction at pc tells a YIELD from an AWAIT.
enum CoroState : uint8_t { CO_START, CO_SUSPENDED, CO_RUNNING, CO_DONE, CO_AWAIT_RETURN };
struct PromiseObj;
struct ClassRT;
// A request to an async generator: next(v) / throw(e) / return(v), queued until the generator
// answers it (the AsyncGeneratorRequest of the specification). Its promise is resolved with an
// IteratorResult record of class `cls` ({ value, done }); a primitive value in a `T | undefined` field
// is boxed with elemVT.
struct AGenRequest {
  PromiseObj* promise;
  ClassRT* cls;
  Slot value;      // the sent value (next / return) or the error (throw)
  uint8_t mode;    // 0 next, 1 throw, 2 return
  uint8_t kind;    // the sent value's kind: 0 none, 1 I, 2 F, 3 R
  uint8_t elemVT;  // the value type of T
};
struct CoroObj : Obj {
  FuncRT* fn;
  CloObj* closure;
  PromiseObj* promise;  // async function: the promise it returns (null for generators)
  uint32_t pc;          // CO_START: the first instruction to run; CO_SUSPENDED: the YIELD / AWAIT it waits at
  uint32_t nregs;
  uint8_t fflags;       // the function's flags (kind and value kinds)
  uint8_t state;        // CoroState
  // an async generator (fflags kind 3): its request queue, null until the first request (its entries
  // are counted in the heap)
  std::deque<AGenRequest>* queue;
  Slot* regs() { return reinterpret_cast<Slot*>(this + 1); }
};
inline bool isAsyncGen(const CoroObj* c) { return (c->fflags & 3) == 3; }

// Runtime class info
struct ClassRT {
  std::string name;
  ClassRT* parent = nullptr;
  bool isRecord = false;
  bool isTuple = false;
  bool presence = false;  // instances carry a presence mask (D7, class flag kClassFlagPresence)
  // records with getters / setters (kClassFlagAccessors): per field, bit 0 a getter, bit 1 a setter (the
  // field holds the accessor pair, see accessorGet in stdlib/util.h); empty for other classes
  std::vector<uint8_t> accessors;
  uint32_t nfields = 0;
  std::vector<uint8_t> fieldVT;
  std::vector<std::string> fieldNames;
  std::vector<uint16_t> refFields;
  std::vector<FuncRT*> vtable;
  std::vector<std::string> methodNames;
  std::unordered_map<std::string, uint32_t> fieldIndex;
  std::unordered_map<std::string, uint32_t> methodIndex;
};

// ---- promises (DESIGN.md D2)
enum PromiseState : uint8_t { PS_PENDING, PS_FULFILLED, PS_REJECTED };
// What happens when a promise settles (a PromiseReaction of the specification, plus the internal
// steps of await, finally and the combinators)
enum ReactionKind : uint8_t {
  RK_AWAIT,          // resume the async function `a` (a coroutine) with the value, or throw the reason into it
  RK_THEN,           // call closure a (fulfilled) / b (rejected), resolve `derived` with the result; no handler: pass through
  RK_THENABLE,       // ResolveThenableJob: `a` is the promise `derived` was resolved with
  RK_FINALLY,        // call closure a(); then settle `derived` like the source promise (after the result settles)
  RK_FINALLY_VALUE,  // the value thunk of finally: fulfill derived with `captured` or reject with it
  RK_ALL,            // Promise.all element `index`: store it in `agg`, fulfill derived when all are in
  RK_ALL_SETTLED,    // Promise.allSettled element: a { status, value } / { status, reason } record
  RK_ANY,            // Promise.any element: fulfill derived with the first value; collect the reasons
  RK_RACE,           // Promise.race: settle derived like the first one to settle
  RK_AGEN_RETURN,    // AsyncGeneratorAwaitReturn: async generator `a` completes its return(v) request
};
struct PromiseObj;
struct Reaction {
  uint8_t kind = RK_THEN;
  uint8_t capturedRejected = 0;
  uint32_t index = 0;
  Obj* a = nullptr;
  Obj* b = nullptr;
  PromiseObj* derived = nullptr;
  Obj* agg = nullptr;  // combinators: the array of results
  Slot captured{};     // RK_FINALLY_VALUE: the source's value / reason
  uint8_t capturedVT = 0;
};
struct PromiseObj : Obj {
  uint8_t state;
  uint8_t vt;               // value type of the fulfilled value
  bool handled;             // a reaction was ever added (rejection tracking)
  bool alreadyResolved;     // resolve / reject functions of `new Promise` were used
  uint32_t remaining;       // combinators: elements still pending (+1 while being set up)
  int32_t classIdx;         // allSettled: record class (module classes of `mod`) for the results
  void* mod;                // allSettled: ModuleRT of the record class
  Slot value;               // fulfilled value (vt) or rejection reason (an Error object)
  std::vector<Reaction> reactions;  // while pending
};

inline uint64_t* InstObj::mask() { return reinterpret_cast<uint64_t*>(fields() + cls->nfields); }
inline const uint64_t* InstObj::mask() const { return reinterpret_cast<const uint64_t*>(fields() + cls->nfields); }
inline size_t presenceWords(uint32_t nfields) { return (nfields + 63) / 64; }

// Whether field i of an instance is an own property: class fields always are; a record field is unless
// the record has a presence mask and the field's bit is clear (a missing optional property). Records
// whose properties can never be absent or hold undefined have no mask.
inline bool fieldPresent(const InstObj* in, uint32_t i) {
  const ClassRT* c = in->cls;
  if (!c->presence) return true;
  return (in->mask()[i / 64] >> (i % 64)) & 1;
}
inline void setFieldPresent(InstObj* in, uint32_t i, bool on) {
  uint64_t bit = 1ull << (i % 64);
  if (on) in->mask()[i / 64] |= bit;
  else in->mask()[i / 64] &= ~bit;
}

}  // namespace e7
