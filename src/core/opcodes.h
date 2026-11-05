// e7vm instruction set.
// Every instruction is one 64-bit word: op:16 | a:16 | b:16 | c:16  (bx = b | c<<16).
// Call-like instructions are followed by extra words (see BYTECODE.md).
#pragma once
#include <cstdint>

namespace e7 {

// X(name, format, kind)
//  format = operand types for a, b, c.
//   'I' 'F' 'R'  register of kind int / float / ref
//   '_'          unused (must be zero)
//   'v'          value-type immediate (VT)
//   'n'          any 16-bit immediate
//   's'          signed 16-bit immediate
//   'c'          class index
//   'e'          field index (checked at run time)
//   'Q'          inline-cache slot of current function
//   'U'          upvalue index of current function
//   'j'          relative jump: signed 16-bit offset from the next instruction
//  bx forms (b and c combined, format[2] == '*'):
//   'J' jump target   'k' int const   'd' number const   't' string const
//   'g' global index  'm' signed 32-bit immediate        'p' function index
//  "***" = special encoding (calls / closures)
//  kind: kind used by 'g'/'U' checks ('-' when not applicable)
#define E7_OPCODES(X) \
  X(NOP, "___", '-') \
  X(MOV_I, "II_", '-') X(MOV_F, "FF_", '-') X(MOV_R, "RR_", '-') \
  X(LOADI, "Im*", '-') X(LOADK_I, "Ik*", '-') X(LOADK_F, "Fd*", '-') X(LOADK_S, "Rt*", '-') \
  X(LOADNULL, "R__", '-') X(LOADUNDEF, "R__", '-') \
  X(GGET_I, "Ig*", 'I') X(GGET_F, "Fg*", 'F') X(GGET_R, "Rg*", 'R') \
  X(GSET_I, "Ig*", 'I') X(GSET_F, "Fg*", 'F') X(GSET_R, "Rg*", 'R') \
  /* number (f64, JS semantics) */ \
  X(ADD_F, "FFF", '-') X(SUB_F, "FFF", '-') X(MUL_F, "FFF", '-') X(DIV_F, "FFF", '-') \
  X(MOD_F, "FFF", '-') X(POW_F, "FFF", '-') X(NEG_F, "FF_", '-') \
  X(BAND_F, "FFF", '-') X(BOR_F, "FFF", '-') X(BXOR_F, "FFF", '-') \
  X(SHL_F, "FFF", '-') X(SAR_F, "FFF", '-') X(SHR_F, "FFF", '-') X(BNOT_F, "FF_", '-') \
  X(EQ_F, "IFF", '-') X(NE_F, "IFF", '-') X(LT_F, "IFF", '-') X(LE_F, "IFF", '-') \
  /* fixed-width integers (64-bit container) */ \
  X(ADD_I, "III", '-') X(SUB_I, "III", '-') X(MUL_I, "III", '-') X(NEG_I, "II_", '-') \
  X(DIVS_I, "III", '-') X(DIVU_I, "III", '-') X(REMS_I, "III", '-') X(REMU_I, "III", '-') \
  X(AND_I, "III", '-') X(OR_I, "III", '-') X(XOR_I, "III", '-') X(NOT_I, "II_", '-') \
  X(SHL_I, "III", '-') X(SAR_I, "III", '-') X(SHR_I, "III", '-') \
  X(SHL32, "III", '-') X(SAR32, "III", '-') X(SHR32, "III", '-') \
  X(ROTL32, "III", '-') X(ROTR32, "III", '-') X(ROTL64, "III", '-') X(ROTR64, "III", '-') \
  X(CLZ32, "II_", '-') X(CLZ64, "II_", '-') X(CTZ32, "II_", '-') X(CTZ64, "II_", '-') \
  X(POPCNT, "II_", '-') X(BSWAP16, "II_", '-') X(BSWAP32, "II_", '-') X(BSWAP64, "II_", '-') \
  X(MULHIU, "III", '-') X(MULHIS, "III", '-') \
  X(WRAP_I8, "II_", '-') X(WRAP_U8, "II_", '-') X(WRAP_I16, "II_", '-') X(WRAP_U16, "II_", '-') \
  X(WRAP_I32, "II_", '-') X(WRAP_U32, "II_", '-') \
  X(EQ_I, "III", '-') X(NE_I, "III", '-') X(LT_I, "III", '-') X(LE_I, "III", '-') \
  X(LTU_I, "III", '-') X(LEU_I, "III", '-') \
  X(ADDI_I, "IIs", '-') X(NOT_B, "II_", '-') \
  /* conversions */ \
  X(I2F, "FI_", '-') X(U2F, "FI_", '-') X(F2I, "IF_", '-') X(F2B, "IF_", '-') \
  /* control flow */ \
  X(JMP, "_J*", '-') X(JT, "IJ*", '-') X(JF, "IJ*", '-') \
  /* fused compare-and-branch; c = signed 16-bit offset relative to the next instruction */ \
  X(JLT_F, "FFj", '-') X(JNLT_F, "FFj", '-') X(JLE_F, "FFj", '-') X(JNLE_F, "FFj", '-') \
  X(JEQ_F, "FFj", '-') X(JNE_F, "FFj", '-') \
  X(JLT_I, "IIj", '-') X(JNLT_I, "IIj", '-') X(JLE_I, "IIj", '-') X(JNLE_I, "IIj", '-') \
  X(JEQ_I, "IIj", '-') X(JNE_I, "IIj", '-') \
  X(JNULLISH, "RJ*", '-') X(JNOTNULLISH, "RJ*", '-') \
  X(ISNULL, "IR_", '-') X(ISUNDEF, "IR_", '-') X(ISNULLISH, "IR_", '-') \
  X(RET_I, "I__", '-') X(RET_F, "F__", '-') X(RET_R, "R__", '-') X(RET_V, "___", '-') \
  X(CALL, "***", '-') X(CALLV, "***", '-') X(CALLM, "***", '-') X(CALLI, "***", '-') \
  X(CALLN, "***", '-') X(CLOSURE, "***", '-') \
  /* cells and upvalues */ \
  X(NEWCELL, "Rv_", '-') \
  X(CGET_I, "IR_", 'I') X(CGET_F, "FR_", 'F') X(CGET_R, "RR_", 'R') \
  X(CSET_I, "RI_", 'I') X(CSET_F, "RF_", 'F') X(CSET_R, "RR_", 'R') \
  X(UGET_I, "IU_", 'I') X(UGET_F, "FU_", 'F') X(UGET_R, "RU_", 'R') \
  X(USET_I, "UI_", 'I') X(USET_F, "UF_", 'F') X(USET_R, "UR_", 'R') \
  X(UCELL, "RU_", '-') \
  /* objects */ \
  X(NEW, "Rc_", '-') \
  X(GETF_I, "IRe", 'I') X(GETF_F, "FRe", 'F') X(GETF_R, "RRe", 'R') \
  X(SETF_I, "ReI", 'I') X(SETF_F, "ReF", 'F') X(SETF_R, "ReR", 'R') \
  X(GETN_I, "IRQ", 'I') X(GETN_F, "FRQ", 'F') X(GETN_R, "RRQ", 'R') \
  X(SETN_I, "RQI", 'I') X(SETN_F, "RQF", 'F') X(SETN_R, "RQR", 'R') \
  X(INSTOF, "IRc", '-') \
  /* boxes (optional primitives) */ \
  X(BOX_I, "RIv", '-') X(BOX_F, "RF_", '-') X(UNBOX_I, "IR_", '-') X(UNBOX_F, "FR_", '-') \
  X(CHKNN, "R__", '-') \
  /* arrays */ \
  X(NEWARR, "Rvn", '-') \
  X(AGET_I, "IRF", 'I') X(AGET_F, "FRF", 'F') X(AGET_R, "RRF", 'R') \
  X(AGETI_I, "IRI", 'I') X(AGETI_F, "FRI", 'F') X(AGETI_R, "RRI", 'R') \
  X(ASET_I, "RFI", 'I') X(ASET_F, "RFF", 'F') X(ASET_R, "RFR", 'R') \
  X(ASETI_I, "RII", 'I') X(ASETI_F, "RIF", 'F') X(ASETI_R, "RIR", 'R') \
  X(APUSH_I, "RI_", 'I') X(APUSH_F, "RF_", 'F') X(APUSH_R, "RR_", 'R') \
  X(ALEN, "FR_", '-') X(ALENI, "IR_", '-') \
  /* byte views (Uint8Array) */ \
  X(BGET, "FRF", '-') X(BGETI, "IRI", '-') X(BSET, "RFF", '-') X(BSETI, "RII", '-') \
  X(BLEN, "FR_", '-') X(BLENI, "IR_", '-') \
  /* strings */ \
  X(SLEN, "FR_", '-') X(SCAT, "RRR", '-') \
  X(SEQ, "IRR", '-') X(SNE, "IRR", '-') X(SLT, "IRR", '-') X(SLE, "IRR", '-') \
  X(NUM2STR, "RF_", '-') X(INT2STR, "RIv", '-') X(BOOL2STR, "RI_", '-') \
  X(STRTRUTHY, "IR_", '-') \
  X(EQ_R, "IRR", '-') X(NE_R, "IRR", '-') \
  X(ABORT, "R__", '-') \
  /* `debugger;` statement: no-op unless a debugger is attached */ \
  X(DEBUGGER, "___", '-') \
  /* throw R[A] (an Error object); try/catch handlers are in the function's handler table */ \
  X(THROW, "R__", '-')

enum Op : uint16_t {
#define E7_X(name, fmt, kind) OP_##name,
  E7_OPCODES(E7_X)
#undef E7_X
  OP__COUNT
};

struct OpInfo { const char* name; const char* fmt; char kind; };
extern const OpInfo kOpInfo[OP__COUNT];

inline uint64_t encode(uint16_t op, uint16_t a = 0, uint16_t b = 0, uint16_t c = 0) {
  return (uint64_t)op | ((uint64_t)a << 16) | ((uint64_t)b << 32) | ((uint64_t)c << 48);
}
inline uint64_t encodeBx(uint16_t op, uint16_t a, uint32_t bx) {
  return (uint64_t)op | ((uint64_t)a << 16) | ((uint64_t)bx << 32);
}
inline uint16_t insOp(uint64_t w) { return (uint16_t)w; }
inline uint16_t insA(uint64_t w) { return (uint16_t)(w >> 16); }
inline uint16_t insB(uint64_t w) { return (uint16_t)(w >> 32); }
inline uint16_t insC(uint64_t w) { return (uint16_t)(w >> 48); }
inline uint32_t insBx(uint64_t w) { return (uint32_t)(w >> 32); }

constexpr uint16_t kNoReg = 0xFFFF;  // "no destination" for void calls

}  // namespace e7
