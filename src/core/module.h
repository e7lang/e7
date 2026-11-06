// In-memory representation of a compiled E7 module (the content of a .e7b file).
#pragma once
#include "common.h"
#include "opcodes.h"

namespace e7 {

// The e7b format identifier this code reads and writes (the header's format field). The loader accepts
// exactly this value; see docs/BYTECODE.md.
constexpr uint32_t kFormatVersion = 3;

struct FieldDesc {
  std::string name;
  uint8_t vt = VT_NUMBER;
};

struct ClassDesc {
  std::string name;
  int32_t parent = -1;     // index of parent class or -1
  uint8_t isRecord = 0;    // anonymous object-literal shape
  uint8_t cflags = 0;      // kClassFlag*
  std::vector<FieldDesc> fields;      // full layout incl. inherited fields
  std::vector<uint32_t> vtable;       // function indices (incl. inherited)
  std::vector<std::string> methodNames;  // parallel to vtable
};

// Optional debug information of a function (compiled with debug info; see docs/BYTECODE.md).
struct DebugPos {      // source position of the code from `pc` on (until the next entry)
  uint32_t pc = 0, line = 0, col = 0;
};
struct DebugLocal {    // a named local variable (or parameter) living in a register
  std::string name;
  uint32_t reg = 0;
  uint32_t startPc = 0, endPc = 0;  // live range [startPc, endPc)
  uint8_t vt = 0;                   // exact value type (VT_*) of the value
  uint8_t flags = 0;                // kDebugLocalCell: the register holds a cell (captured variable)
};
constexpr uint8_t kDebugLocalCell = 1;
constexpr uint8_t kDebugLocalParam = 2;
struct FuncDebug {
  std::vector<DebugPos> pos;        // sorted by pc (instruction -> line/column)
  std::vector<DebugPos> stmts;      // statement boundaries (strictly increasing pc), with the statement's position
  std::vector<DebugLocal> locals;
  std::vector<std::string> upvalNames;  // one per upvalue (may be empty: no names)
};

// try/catch: while pc is in [start, end), a catchable runtime error jumps to `handler` with the
// error object in register `reg` (a reference register). Inner handlers are listed before the
// handlers that enclose them; ranges are either nested or disjoint.
struct HandlerDesc {
  uint32_t start = 0, end = 0, handler = 0;
  uint32_t reg = 0;
};

// ClassDesc::cflags: the instances carry a presence mask (D7, records only)
constexpr uint8_t kClassFlagPresence = 1;
// ClassDesc::cflags: a record (object literal) with getters / setters. Field x of the record is an
// accessor property when the vtable has a method "get x" or "set x"; the field holds the accessor pair, a
// tuple [getter closure or null, setter closure or null, slot of the property's value type] (BYTECODE.md 6.5)
constexpr uint8_t kClassFlagAccessors = 2;

struct FuncDesc {
  std::string name;
  uint32_t sig = 0;                 // index into Module::sigs
  uint16_t nparams = 0;
  uint8_t fflags = 0;               // function kind and coroutine value kinds (opcodes.h)
  std::vector<uint8_t> regKinds;    // Kind per register; first nparams are params
  std::vector<uint8_t> upvalKinds;  // Kind of each captured cell's content
  std::vector<uint32_t> icNames;    // string-constant index per inline-cache slot
  std::vector<uint64_t> code;
  std::vector<HandlerDesc> handlers;  // try/catch ranges (innermost first)
  std::vector<uint32_t> lines;      // pairs (pc, line), sorted by pc; optional extension below
  bool hasDebug = false;            // debug information present (`debug` is meaningful)
  FuncDebug debug;
};

// Line-table extension (lookups that stop at the first pc greater than the current one never look at
// it). After the (pc, line) pairs a function may carry
//   (kLineExtMarker, kLineExtFiles) followed by (kLineExtMarker, pc), (kLineExtMarker, strIndex)...
// meaning: code from `pc` on comes from the source file named by string constant `strIndex`
// (relative to the entry file). Without segments, all code is from the module's entry file.
constexpr uint32_t kLineExtMarker = 0xFFFFFFFFu;
constexpr uint32_t kLineExtFiles = 0x53454C46u;  // "FLES"
// Debug-information extension (after the file segments, if any): (kLineExtMarker, kLineExtDebug),
// a word count n (< 2^31), then n payload words (see docs/BYTECODE.md). The file-segment scan stops at
// the first group that is not (marker, pc, marker, index).
constexpr uint32_t kLineExtDebug = 0x31474244u;  // "DBG1"
// Module header flag: the module carries debug information (informational; unknown flags are ignored).
constexpr uint32_t kModuleFlagDebug = 1;

struct NativeImport {
  std::string name;
  uint32_t sig = 0;
};

struct GlobalDesc {
  std::string name;
  uint8_t vt = VT_NUMBER;
};

struct ExportDesc {
  std::string name;
  uint32_t func = 0;
  // host marshaling signature: one char per parameter, '>' and return char.
  //  n number  b boolean  s string  y Uint8Array  h KeyHandle  v void (return only)
  //  1 i8  2 u8  3 i16  4 u16  5 i32  6 u32  7 i64  8 u64   x not marshalable
  std::string hostSig;
};

struct RegExpDesc {
  std::string source;  // UTF-8
  std::string flags;   // a subset of "dgimsuy"
};

struct Module {
  std::string name;
  std::vector<int64_t> ints;
  std::vector<double> nums;
  std::vector<std::u16string> strs;
  std::vector<std::string> sigs;  // e.g. "FR>I" : param kinds '>' return kind (V = void)
  std::vector<NativeImport> natives;
  std::vector<RegExpDesc> regexps;
  std::vector<ClassDesc> classes;
  std::vector<FuncDesc> funcs;
  std::vector<GlobalDesc> globals;
  std::vector<ExportDesc> exports;
  uint32_t initFunc = 0;
  uint32_t flags = 0;     // kModuleFlag*
  uint32_t minVm = 0;     // minimum e7vm version (major << 16 | minor << 8 | patch); see core/since.h
};

// Header fields readable without parsing the whole module (and before verifying a signature).
struct ModuleHeader {
  uint32_t format = 0;
  uint32_t flags = 0;
  uint32_t minVm = 0;
};
// Returns false if data is not an e7b file (bad magic or too short); does not check the CRC.
bool readModuleHeader(const uint8_t* data, size_t len, ModuleHeader& out);

// Signature string helpers
inline std::string sigParams(const std::string& sig) { return sig.substr(0, sig.find('>')); }
inline char sigRet(const std::string& sig) { return sig.empty() ? 'V' : sig.back(); }

// Serialization (core/serialize.cpp)
std::vector<uint8_t> serializeModule(const Module& m);
// Returns false and fills err on malformed input. Never reads out of bounds.
bool deserializeModule(const uint8_t* data, size_t len, Module& out, std::string& err);

// Bytecode verifier (core/verify.cpp). Requires native signatures to be known
// by the caller; checks structural and kind safety of all code.
bool verifyModule(const Module& m, std::string& err);

// Disassembler (core/disasm.cpp)
std::string disassemble(const Module& m);

}  // namespace e7
