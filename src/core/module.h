// In-memory representation of a compiled E7 module (the content of a .e7b file).
#pragma once
#include "common.h"
#include "opcodes.h"

namespace e7 {

constexpr uint32_t kFormatVersion = 1;

struct FieldDesc {
  std::string name;
  uint8_t vt = VT_NUMBER;
};

struct ClassDesc {
  std::string name;
  int32_t parent = -1;     // index of parent class or -1
  uint8_t isRecord = 0;    // anonymous object-literal shape
  std::vector<FieldDesc> fields;      // full layout incl. inherited fields
  std::vector<uint32_t> vtable;       // function indices (incl. inherited)
  std::vector<std::string> methodNames;  // parallel to vtable
};

struct FuncDesc {
  std::string name;
  uint32_t sig = 0;                 // index into Module::sigs
  uint16_t nparams = 0;
  std::vector<uint8_t> regKinds;    // Kind per register; first nparams are params
  std::vector<uint8_t> upvalKinds;  // Kind of each captured cell's content
  std::vector<uint32_t> icNames;    // string-constant index per inline-cache slot
  std::vector<uint64_t> code;
  std::vector<uint32_t> lines;      // pairs (pc, line), sorted by pc
};

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

struct Module {
  std::string name;
  std::vector<int64_t> ints;
  std::vector<double> nums;
  std::vector<std::u16string> strs;
  std::vector<std::string> sigs;  // e.g. "FR>I" : param kinds '>' return kind (V = void)
  std::vector<NativeImport> natives;
  std::vector<ClassDesc> classes;
  std::vector<FuncDesc> funcs;
  std::vector<GlobalDesc> globals;
  std::vector<ExportDesc> exports;
  uint32_t initFunc = 0;
};

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
