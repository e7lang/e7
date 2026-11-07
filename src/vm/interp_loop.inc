// The interpreter loop. Included twice by interp.cpp: as VM::runNormal() (E7_INTERP_DBG 0) and as
// VM::runDebug() (E7_INTERP_DBG 1, used only while a debugger is attached; it checks the debug flags of
// every instruction before executing it). Two ordinary functions rather than a template, so that the
// compiler treats the normal loop exactly as before (e.g. hot/cold splitting is not done for the
// COMDAT sections of template instantiations).
int VM::E7_INTERP_NAME() {
  constexpr bool DBG = E7_INTERP_DBG != 0;
  Frame* fr;
  FuncRT* fn;
  ModuleRT* M;
  const uint64_t* ip;
  Slot* R;
  uint64_t ins;

#define LOAD_FRAME()          \
  do {                        \
    fr = &frames_.back();     \
    fn = fr->fn;              \
    M = fn->mod;              \
    ip = fr->ip;              \
    R = stack_ + fr->base;    \
  } while (0)

#define A ((uint16_t)(ins >> 16))
#define B ((uint16_t)(ins >> 32))
#define C ((uint16_t)(ins >> 48))
#define BX ((uint32_t)(ins >> 32))
#define THROW(...)        \
  do {                    \
    fr->ip = ip;          \
    error(__VA_ARGS__);   \
  } while (0)
#define THROWC(code, ...)        \
  do {                           \
    fr->ip = ip;                 \
    errorCode(code, __VA_ARGS__); \
  } while (0)

#define TICK(n)                                                    \
  do {                                                             \
    budget_ -= (n);                                                \
    if (E7_UNLIKELY(budget_ < 0 || gcRequested_ || kGCStress)) {   \
      fr->ip = ip;                                                 \
      if ((gcRequested_ || kGCStress) && gcBlockers_ == 0) collectGarbage(); \
      if (budget_ < 0) {                                           \
        if (cfg_.suspendOnBudget && nativeDepth_ == 0) return ERR_SUSPENDED; \
        if (E7_UNLIKELY(testCallbackRefill_ > 0) && nativeDepth_ > 0) { \
          setBudget(testCallbackRefill_); /* testing hook (vm.h) */ \
        } else {                                                   \
          budget_ = 0;                                             \
          errorCode(ERR_BUDGET, "instruction budget exhausted");   \
        }                                                          \
      }                                                            \
    }                                                              \
  } while (0)

#define JUMP_TO(target)                                    \
  do {                                                     \
    const uint64_t* nip = fn->code + (target);             \
    if (nip < ip) {                                        \
      int64_t d_ = ip - nip;                               \
      ip = nip;                                            \
      TICK(d_);                                            \
    } else {                                               \
      ip = nip;                                            \
    }                                                      \
  } while (0)

#if E7_COMPUTED_GOTO
  static void* const labels[OP__COUNT] = {
#define E7_X(name, fmt, kind) &&L_##name,
      E7_OPCODES(E7_X)
#undef E7_X
  };
#define CASE(name) L_##name:
#define DISPATCH()                 \
  do {                             \
    DBG_HOOK();                    \
    ins = *ip++;                   \
    goto* labels[(uint16_t)ins];   \
  } while (0)
#else
#define CASE(name) case OP_##name:
#define DISPATCH() goto dispatch
#endif
  // debug interpreter only: stop here if a flag of this instruction is enabled in dbgMask_
#define DBG_HOOK()                                                                                  \
  do {                                                                                              \
    if (DBG && E7_UNLIKELY(fn->dbgFlags[(size_t)(ip - fn->code)] &                                  \
                           dbgMask_.load(std::memory_order_relaxed))) {                             \
      fr->ip = ip;                                                                                  \
      int dst_ = dbg_->check(ip);                                                                   \
      if (dst_) return dst_;                                                                        \
    }                                                                                               \
  } while (0)

  LOAD_FRAME();
#if E7_COMPUTED_GOTO
  DISPATCH();
#else
dispatch:
  DBG_HOOK();
  ins = *ip++;
  switch ((uint16_t)ins) {
#endif

  CASE(NOP) DISPATCH();
  CASE(MOV_I) R[A].i = R[B].i; DISPATCH();
  CASE(MOV_F) R[A].f = R[B].f; DISPATCH();
  CASE(MOV_R) R[A].r = R[B].r; DISPATCH();
  CASE(LOADI) R[A].i = (int64_t)(int32_t)BX; DISPATCH();
  CASE(LOADK_I) R[A].i = M->m.ints[BX]; DISPATCH();
  CASE(LOADK_F) R[A].f = M->m.nums[BX]; DISPATCH();
  CASE(LOADK_S) R[A].r = M->strs[BX]; DISPATCH();
  CASE(LOADNULL) R[A].r = nullptr; DISPATCH();
  CASE(LOADUNDEF) R[A].r = undef_; DISPATCH();
  CASE(GGET_I) CASE(GGET_F) CASE(GGET_R) R[A] = M->globals[BX]; DISPATCH();
  CASE(GSET_I) CASE(GSET_F) CASE(GSET_R) M->globals[BX] = R[A]; DISPATCH();

  // ---- number
  CASE(ADD_F) R[A].f = R[B].f + R[C].f; DISPATCH();
  CASE(SUB_F) R[A].f = R[B].f - R[C].f; DISPATCH();
  CASE(MUL_F) R[A].f = R[B].f * R[C].f; DISPATCH();
  CASE(DIV_F) R[A].f = R[B].f / R[C].f; DISPATCH();
  CASE(MOD_F) {
    double x = R[B].f, y = R[C].f;
    // fast path: both operands are integers that fit in 32 bits
    // (range checks come first: converting an out-of-range double to int is undefined behaviour)
    if (E7_LIKELY(x >= -2147483648.0 && x <= 2147483647.0 && y >= 1.0 && y <= 2147483647.0 &&
                  (double)(int32_t)x == x && (double)(int32_t)y == y)) {
      int32_t xi = (int32_t)x, yi = (int32_t)y;
      int32_t r = xi % yi;
      R[A].f = (r == 0 && (xi < 0 || (xi == 0 && std::signbit(x)))) ? -0.0 : (double)r;
    } else if (std::isinf(y) && std::isfinite(x)) {
      R[A].f = x;
    } else {
      R[A].f = std::fmod(x, y);
    }
    DISPATCH();
  }
  CASE(POW_F) R[A].f = jsPow(R[B].f, R[C].f); DISPATCH();
  CASE(NEG_F) R[A].f = -R[B].f; DISPATCH();
  CASE(BAND_F) R[A].f = (double)(toInt32(R[B].f) & toInt32(R[C].f)); DISPATCH();
  CASE(BOR_F) R[A].f = (double)(toInt32(R[B].f) | toInt32(R[C].f)); DISPATCH();
  CASE(BXOR_F) R[A].f = (double)(toInt32(R[B].f) ^ toInt32(R[C].f)); DISPATCH();
  CASE(SHL_F) R[A].f = (double)(int32_t)((uint32_t)toInt32(R[B].f) << (toUint32(R[C].f) & 31)); DISPATCH();
  CASE(SAR_F) R[A].f = (double)(toInt32(R[B].f) >> (toUint32(R[C].f) & 31)); DISPATCH();
  CASE(SHR_F) R[A].f = (double)(toUint32(R[B].f) >> (toUint32(R[C].f) & 31)); DISPATCH();
  CASE(BNOT_F) R[A].f = (double)(~toInt32(R[B].f)); DISPATCH();
  CASE(EQ_F) R[A].i = R[B].f == R[C].f; DISPATCH();
  CASE(NE_F) R[A].i = R[B].f != R[C].f; DISPATCH();
  CASE(LT_F) R[A].i = R[B].f < R[C].f; DISPATCH();
  CASE(LE_F) R[A].i = R[B].f <= R[C].f; DISPATCH();

  // ---- integers
  CASE(ADD_I) R[A].u = R[B].u + R[C].u; DISPATCH();
  CASE(SUB_I) R[A].u = R[B].u - R[C].u; DISPATCH();
  CASE(MUL_I) R[A].u = R[B].u * R[C].u; DISPATCH();
  CASE(NEG_I) R[A].u = 0 - R[B].u; DISPATCH();
  CASE(DIVS_I) {
    int64_t y = R[C].i;
    if (E7_UNLIKELY(y == 0)) THROW("integer division by zero");
    int64_t x = R[B].i;
    R[A].i = (y == -1) ? (int64_t)(0 - (uint64_t)x) : x / y;
    DISPATCH();
  }
  CASE(DIVU_I) {
    uint64_t y = R[C].u;
    if (E7_UNLIKELY(y == 0)) THROW("integer division by zero");
    R[A].u = R[B].u / y;
    DISPATCH();
  }
  CASE(REMS_I) {
    int64_t y = R[C].i;
    if (E7_UNLIKELY(y == 0)) THROW("integer division by zero");
    R[A].i = (y == -1) ? 0 : R[B].i % y;
    DISPATCH();
  }
  CASE(REMU_I) {
    uint64_t y = R[C].u;
    if (E7_UNLIKELY(y == 0)) THROW("integer division by zero");
    R[A].u = R[B].u % y;
    DISPATCH();
  }
  CASE(AND_I) R[A].u = R[B].u & R[C].u; DISPATCH();
  CASE(OR_I) R[A].u = R[B].u | R[C].u; DISPATCH();
  CASE(XOR_I) R[A].u = R[B].u ^ R[C].u; DISPATCH();
  CASE(NOT_I) R[A].u = ~R[B].u; DISPATCH();
  CASE(SHL_I) R[A].u = R[B].u << (R[C].u & 63); DISPATCH();
  CASE(SAR_I) R[A].i = R[B].i >> (R[C].u & 63); DISPATCH();
  CASE(SHR_I) R[A].u = R[B].u >> (R[C].u & 63); DISPATCH();
  CASE(SHL32) R[A].u = (uint64_t)((uint32_t)R[B].u << (R[C].u & 31)); DISPATCH();
  CASE(SAR32) R[A].i = (int64_t)((int32_t)R[B].u >> (R[C].u & 31)); DISPATCH();
  CASE(SHR32) R[A].u = (uint64_t)((uint32_t)R[B].u >> (R[C].u & 31)); DISPATCH();
  CASE(ROTL32) R[A].u = rotl32((uint32_t)R[B].u, (unsigned)R[C].u); DISPATCH();
  CASE(ROTR32) R[A].u = rotl32((uint32_t)R[B].u, (32u - ((unsigned)R[C].u & 31)) & 31); DISPATCH();
  CASE(ROTL64) R[A].u = rotl64(R[B].u, (unsigned)R[C].u); DISPATCH();
  CASE(ROTR64) R[A].u = rotl64(R[B].u, (64u - ((unsigned)R[C].u & 63)) & 63); DISPATCH();
  CASE(CLZ32) R[A].i = clz64((uint32_t)R[B].u) - 32; DISPATCH();
  CASE(CLZ64) R[A].i = clz64(R[B].u); DISPATCH();
  CASE(CTZ32) { uint32_t x = (uint32_t)R[B].u; R[A].i = x ? ctz64(x) : 32; DISPATCH(); }
  CASE(CTZ64) R[A].i = ctz64(R[B].u); DISPATCH();
  CASE(POPCNT) R[A].i = popcnt64(R[B].u); DISPATCH();
  CASE(BSWAP16) { uint16_t x = (uint16_t)R[B].u; R[A].u = (uint16_t)((x >> 8) | (x << 8)); DISPATCH(); }
  CASE(BSWAP32) {
    uint32_t x = (uint32_t)R[B].u;
    R[A].u = (x >> 24) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | (x << 24);
    DISPATCH();
  }
  CASE(BSWAP64) {
    uint64_t x = R[B].u, y = 0;
    for (int k = 0; k < 8; k++) { y = (y << 8) | (x & 0xFF); x >>= 8; }
    R[A].u = y;
    DISPATCH();
  }
  CASE(MULHIU) R[A].u = mulhiu(R[B].u, R[C].u); DISPATCH();
  CASE(MULHIS) R[A].i = mulhis(R[B].i, R[C].i); DISPATCH();
  CASE(WRAP_I8) R[A].i = (int8_t)R[B].u; DISPATCH();
  CASE(WRAP_U8) R[A].u = (uint8_t)R[B].u; DISPATCH();
  CASE(WRAP_I16) R[A].i = (int16_t)R[B].u; DISPATCH();
  CASE(WRAP_U16) R[A].u = (uint16_t)R[B].u; DISPATCH();
  CASE(WRAP_I32) R[A].i = (int32_t)R[B].u; DISPATCH();
  CASE(WRAP_U32) R[A].u = (uint32_t)R[B].u; DISPATCH();
  CASE(EQ_I) R[A].i = R[B].i == R[C].i; DISPATCH();
  CASE(NE_I) R[A].i = R[B].i != R[C].i; DISPATCH();
  CASE(LT_I) R[A].i = R[B].i < R[C].i; DISPATCH();
  CASE(LE_I) R[A].i = R[B].i <= R[C].i; DISPATCH();
  CASE(LTU_I) R[A].i = R[B].u < R[C].u; DISPATCH();
  CASE(LEU_I) R[A].i = R[B].u <= R[C].u; DISPATCH();
  CASE(ADDI_I) R[A].u = R[B].u + (uint64_t)(int64_t)(int16_t)C; DISPATCH();
  CASE(NOT_B) R[A].i = R[B].i == 0; DISPATCH();

  // ---- conversions
  CASE(I2F) R[A].f = (double)R[B].i; DISPATCH();
  CASE(U2F) R[A].f = (double)R[B].u; DISPATCH();
  CASE(F2I) R[A].i = doubleToInt64Bits(R[B].f); DISPATCH();
  CASE(F2B) { double d = R[B].f; R[A].i = !(d == 0 || d != d); DISPATCH(); }

  // ---- control
  CASE(JMP) JUMP_TO(BX); DISPATCH();
  CASE(JT) if (R[A].i) JUMP_TO(BX); DISPATCH();
  CASE(JF) if (!R[A].i) JUMP_TO(BX); DISPATCH();
#define FJUMP(cond)                     \
  do {                                  \
    if (cond) {                         \
      int16_t off_ = (int16_t)C;        \
      ip += off_;                       \
      if (off_ < 0) TICK(-off_);        \
    }                                   \
    DISPATCH();                         \
  } while (0)
  CASE(JLT_F) FJUMP(R[A].f < R[B].f);
  CASE(JNLT_F) FJUMP(!(R[A].f < R[B].f));
  CASE(JLE_F) FJUMP(R[A].f <= R[B].f);
  CASE(JNLE_F) FJUMP(!(R[A].f <= R[B].f));
  CASE(JEQ_F) FJUMP(R[A].f == R[B].f);
  CASE(JNE_F) FJUMP(R[A].f != R[B].f);
  CASE(JLT_I) FJUMP(R[A].i < R[B].i);
  CASE(JNLT_I) FJUMP(!(R[A].i < R[B].i));
  CASE(JLE_I) FJUMP(R[A].i <= R[B].i);
  CASE(JNLE_I) FJUMP(!(R[A].i <= R[B].i));
  CASE(JEQ_I) FJUMP(R[A].i == R[B].i);
  CASE(JNE_I) FJUMP(R[A].i != R[B].i);
  CASE(JNULLISH) { Obj* o = R[A].r; if (!o || o == undef_) JUMP_TO(BX); DISPATCH(); }
  CASE(JNOTNULLISH) { Obj* o = R[A].r; if (o && o != undef_) JUMP_TO(BX); DISPATCH(); }
  CASE(ISNULL) R[A].i = R[B].r == nullptr; DISPATCH();
  CASE(ISUNDEF) R[A].i = R[B].r == undef_; DISPATCH();
  CASE(ISNULLISH) { Obj* o = R[B].r; R[A].i = !o || o == undef_; DISPATCH(); }

// returning from a coroutine's frame: a generator is done (the value and done = 1 go to the resumer)
#define CORO_RET(rv, kc)                                                     \
  do {                                                                       \
    CoroObj* co_ = fr->coro;                                                 \
    if (fflagsKind(co_->fflags) == FK_ASYNC) ASYNC_RET(rv, kc);              \
    if (fflagsKind(co_->fflags) == FK_ASYNC_GENERATOR) AGEN_RET(rv, kc);     \
    co_->state = CO_DONE;                                                    \
    bool ent_ = fr->entry;                                                   \
    uint16_t dst_ = fr->retDst, dd_ = fr->doneDst;                           \
    frames_.pop_back();                                                      \
    if (DBG && E7_UNLIKELY(frames_.size() < dbgRetDepth_)) dbgMask_.fetch_or(DF_ALWAYS); \
    if (ent_) { result_ = rv; resultKind_ = kc; return ERR_OK; }             \
    LOAD_FRAME();                                                            \
    if (dst_ != kNoReg && kc != 'V') R[dst_] = rv;                           \
    if (dd_ != kNoReg) R[dd_].i = 1;                                         \
    DISPATCH();                                                              \
  } while (0)

// returning from an async function's frame: its promise is resolved with the value; the caller (the
// first part of the call) or the job loop (a continuation after an await) gets the promise
#define ASYNC_RET(rv, kc)                                                    \
  do {                                                                       \
    CoroObj* co_ = fr->coro;                                                 \
    PromiseObj* p_ = co_->promise;                                           \
    Slot v_ = (rv);                                                          \
    if (kc == 'V') v_.r = undef_;                                            \
    fr->ip = ip;                                                             \
    co_->state = CO_DONE;                                                    \
    if (kc == 'R' && v_.r && v_.r->type == OT::Promise) adoptPromise(p_, static_cast<PromiseObj*>(v_.r)); \
    else if (kc != 'V' && kindChar(kindOfVT(p_->vt)) != kc) THROW("async function returned a value of the wrong type"); \
    else resolvePromise(p_, v_); /* before the frame is popped: its registers keep the value alive */ \
    bool ent_ = fr->entry;                                                   \
    uint16_t dst_ = fr->retDst;                                              \
    frames_.pop_back();                                                      \
    if (DBG && E7_UNLIKELY(frames_.size() < dbgRetDepth_)) dbgMask_.fetch_or(DF_ALWAYS); \
    if (ent_) { result_.r = p_; resultKind_ = 'R'; return ERR_OK; }         \
    LOAD_FRAME();                                                            \
    if (dst_ != kNoReg) R[dst_].r = p_;                                      \
    DISPATCH();                                                              \
  } while (0)

// returning from an async generator's frame: it is completed, its first request gets
// { value, done: true } and the requests queued after it are answered (AsyncGeneratorDrainQueue)
#define AGEN_RET(rv, kc)                                                     \
  do {                                                                       \
    CoroObj* co_ = fr->coro;                                                 \
    fr->ip = ip;                                                             \
    co_->state = CO_DONE;                                                    \
    /* before the frame is popped: its registers keep the value alive */     \
    agenCompleteStep(co_, false, (rv), kc == 'V' ? 0 : kc == 'I' ? 1 : kc == 'F' ? 2 : 3, true); \
    agenDrainQueue(co_);                                                     \
    bool ent_ = fr->entry;                                                   \
    frames_.pop_back();                                                      \
    if (DBG && E7_UNLIKELY(frames_.size() < dbgRetDepth_)) dbgMask_.fetch_or(DF_ALWAYS); \
    if (ent_) { resultKind_ = 'V'; return ERR_OK; }                         \
    LOAD_FRAME();                                                            \
    DISPATCH();                                                              \
  } while (0)

#define DO_RET(val, kc)                         \
  do {                                          \
    Slot rv = (val);                            \
    if (E7_UNLIKELY(fr->coro != nullptr)) CORO_RET(rv, kc); \
    bool ent = fr->entry;                       \
    uint16_t dst = fr->retDst;                  \
    frames_.pop_back();                         \
    if (DBG && E7_UNLIKELY(frames_.size() < dbgRetDepth_)) dbgMask_.fetch_or(DF_ALWAYS); \
    if (ent) { result_ = rv; resultKind_ = kc; return ERR_OK; } \
    LOAD_FRAME();                               \
    if (dst != kNoReg) R[dst] = rv;             \
    DISPATCH();                                 \
  } while (0)

  CASE(RET_I) DO_RET(R[A], 'I');
  CASE(RET_F) DO_RET(R[A], 'F');
  CASE(RET_R) DO_RET(R[A], 'R');
  CASE(RET_V) { Slot z; z.u = 0; DO_RET(z, 'V'); }

#define PUSH_CALL(callee, closure, dst, nextIp, ARGCOPY)                                      \
  do {                                                                                     \
    FuncRT* cf_ = (callee);                                                                \
    uint32_t nb_ = fr->base + fn->nregs;                                                   \
    if (E7_UNLIKELY((size_t)nb_ + cf_->nregs > cfg_.stackSlots || frames_.size() >= cfg_.maxFrames)) \
      THROWC(ERR_STACK, "stack overflow");                                                 \
    Slot* NR = stack_ + nb_;                                                               \
    ARGCOPY;                                                                               \
    for (uint16_t r_ : cf_->refInit) NR[r_].r = nullptr;                                   \
    fr->ip = (nextIp);                                                                     \
    frames_.push_back(Frame{cf_, cf_->code, (closure), nb_, (dst), false});                \
    LOAD_FRAME();                                                                          \
    TICK(1);                                                                               \
  } while (0)

  CASE(CALL) {
    FuncRT* callee = &M->funcs[B];
    uint32_t argc = C;
    const uint64_t* aw = ip;
    const uint64_t* next = ip + ((argc + 3) >> 2);
    PUSH_CALL(callee, nullptr, A, next, for (uint32_t k = 0; k < argc; k++) NR[k] = R[argReg(aw, k)]);
    DISPATCH();
  }
  CASE(CALLV) {
    uint32_t argc = C;
    uint32_t sig = (uint32_t)ip[0];
    const uint64_t* aw = ip + 1;
    const uint64_t* next = aw + ((argc + 3) >> 2);
    Obj* o = R[B].r;
    if (E7_UNLIKELY(!o || o->type != OT::Closure)) { ip = next; THROW("called value is not a function (null or undefined)"); }
    auto* clo = static_cast<CloObj*>(o);
    if (E7_UNLIKELY(clo->fn->sigId != M->sigMap[sig])) { ip = next; THROW("function signature mismatch"); }
    PUSH_CALL(clo->fn, clo, A, next, for (uint32_t k = 0; k < argc; k++) NR[k] = R[argReg(aw, k)]);
    DISPATCH();
  }
  CASE(CALLM) {
    uint32_t argc = C;
    uint32_t vslot = (uint32_t)ip[0];
    uint32_t sig = (uint32_t)(ip[0] >> 32);
    const uint64_t* aw = ip + 1;
    const uint64_t* next = aw + ((argc + 3) >> 2);
    Obj* o = R[B].r;
    if (E7_UNLIKELY(!o || o->type != OT::Instance)) { ip = next; THROW("cannot call method on null or undefined"); }
    ClassRT* cls = static_cast<InstObj*>(o)->cls;
    if (E7_UNLIKELY(vslot >= cls->vtable.size())) { ip = next; THROW("invalid method slot"); }
    FuncRT* callee = cls->vtable[vslot];
    if (E7_UNLIKELY(callee->sigId != M->sigMap[sig])) { ip = next; THROW("method signature mismatch"); }
    Slot self = R[B];
    PUSH_CALL(callee, nullptr, A, next, NR[0] = self; for (uint32_t k = 0; k < argc; k++) NR[k + 1] = R[argReg(aw, k)]);
    DISPATCH();
  }
  CASE(CALLI) {
    uint32_t argc = C;
    uint32_t icSlot = (uint32_t)ip[0];
    uint32_t sig = (uint32_t)(ip[0] >> 32);
    const uint64_t* aw = ip + 1;
    const uint64_t* next = aw + ((argc + 3) >> 2);
    Obj* o = R[B].r;
    if (E7_UNLIKELY(!o || o->type != OT::Instance)) { ip = next; THROW("cannot call method on null or undefined"); }
    ClassRT* cls = static_cast<InstObj*>(o)->cls;
    InlineCache& ic = fn->ics[icSlot];
    if (E7_UNLIKELY(ic.cls != cls)) {
      std::string nm = utf16ToUtf8(M->m.strs[ic.name]);
      auto it = cls->methodIndex.find(nm);
      if (it == cls->methodIndex.end()) { ip = next; THROW("object has no method '%s'", nm.c_str()); }
      ic.cls = cls;
      ic.index = it->second;
    }
    if (E7_UNLIKELY(ic.index >= cls->vtable.size())) { ip = next; THROW("invalid method slot"); }
    FuncRT* callee = cls->vtable[ic.index];
    if (E7_UNLIKELY(callee->sigId != M->sigMap[sig])) { ip = next; THROW("method signature mismatch"); }
    Slot self = R[B];
    PUSH_CALL(callee, nullptr, A, next, NR[0] = self; for (uint32_t k = 0; k < argc; k++) NR[k + 1] = R[argReg(aw, k)]);
    DISPATCH();
  }
  CASE(CALLN) {
    NativeBinding& nb = M->natives[B];
    uint32_t argc = C;
    const uint64_t* aw = ip;
    ip += (argc + 3) >> 2;
    fr->ip = ip;
    Slot ret;
    if (E7_LIKELY(argc <= 16)) {
      Slot args[16];
      for (uint32_t k = 0; k < argc; k++) args[k] = R[argReg(aw, k)];
      ret.u = 0;
      nativeDepth_++;
      gcBlockers_++;
      const NativeBinding* prevNative = curNative_;
      curNative_ = &nb;
      if (E7_UNLIKELY(nb.cost != NC_NONE)) callCharged(nb, args, argc, &ret);
      else nb.fn(*this, args, argc, &ret, nb.ud);
      curNative_ = prevNative;
      gcBlockers_--;
      nativeDepth_--;
    } else {
      ret = invokeNativeMany(nb, R, aw, argc);
    }
    if (A != kNoReg) R[A] = ret;
    DISPATCH();
  }
  CASE(CLOSURE) {
    FuncRT* target = &M->funcs[B];
    uint32_t ncap = C;
    const uint64_t* aw = ip;
    ip += (ncap + 3) >> 2;
    CloObj* clo = newClosure(target, ncap);
    for (uint32_t k = 0; k < ncap; k++) {
      uint16_t spec = argReg(aw, k);
      Obj* cell;
      if (spec & 0x8000) {
        uint32_t ui = spec & 0x7FFF;
        if (!fr->closure || ui >= fr->closure->nup) THROW("invalid upvalue capture");
        cell = fr->closure->ups()[ui];
      } else {
        cell = R[spec].r;
      }
      if (!cell || cell->type != OT::Cell ||
          kindOfVT(static_cast<CellObj*>(cell)->vt) != target->d->upvalKinds[k])
        THROW("invalid closure capture");
      clo->ups()[k] = static_cast<CellObj*>(cell);
    }
    R[A].r = clo;
    DISPATCH();
  }

  // ---- cells
  CASE(NEWCELL) R[A].r = newCell((uint8_t)B); DISPATCH();
#define CELL_CHECK(o, K)                                                                    \
  if (E7_UNLIKELY(!(o) || (o)->type != OT::Cell || kindOfVT(static_cast<CellObj*>(o)->vt) != (K))) \
    THROW("invalid cell access");
  CASE(CGET_I) { Obj* o = R[B].r; CELL_CHECK(o, K_I); R[A] = static_cast<CellObj*>(o)->v; DISPATCH(); }
  CASE(CGET_F) { Obj* o = R[B].r; CELL_CHECK(o, K_F); R[A] = static_cast<CellObj*>(o)->v; DISPATCH(); }
  CASE(CGET_R) { Obj* o = R[B].r; CELL_CHECK(o, K_R); R[A] = static_cast<CellObj*>(o)->v; DISPATCH(); }
  CASE(CSET_I) { Obj* o = R[A].r; CELL_CHECK(o, K_I); static_cast<CellObj*>(o)->v = R[B]; DISPATCH(); }
  CASE(CSET_F) { Obj* o = R[A].r; CELL_CHECK(o, K_F); static_cast<CellObj*>(o)->v = R[B]; DISPATCH(); }
  CASE(CSET_R) { Obj* o = R[A].r; CELL_CHECK(o, K_R); static_cast<CellObj*>(o)->v = R[B]; DISPATCH(); }
#define UPV(idx) (fr->closure->ups()[idx])
#define UP_CHECK(idx) if (E7_UNLIKELY(!fr->closure || (idx) >= fr->closure->nup)) THROW("invalid upvalue");
  CASE(UGET_I) CASE(UGET_F) CASE(UGET_R) { UP_CHECK(B); R[A] = UPV(B)->v; DISPATCH(); }
  CASE(USET_I) CASE(USET_F) CASE(USET_R) { UP_CHECK(A); UPV(A)->v = R[B]; DISPATCH(); }
  CASE(UCELL) { UP_CHECK(B); R[A].r = UPV(B); DISPATCH(); }

  // ---- objects
  CASE(NEW) R[A].r = newInstance(&M->classes[B]); DISPATCH();
#define INST_CHECK(o)                                                                   \
  if (E7_UNLIKELY(!(o) || (o)->type != OT::Instance))                                   \
    THROW((o) ? "value is not an object" : "cannot access property of null or undefined");
#define FIELD_CHECK(in, idx, K)                                                         \
  if (E7_UNLIKELY((idx) >= (in)->cls->nfields || kindOfVT((in)->cls->fieldVT[idx]) != (K))) \
    THROW("invalid field access");
  CASE(GETF_I) { Obj* o = R[B].r; INST_CHECK(o); auto* in = static_cast<InstObj*>(o); FIELD_CHECK(in, C, K_I); R[A] = in->fields()[C]; DISPATCH(); }
  CASE(GETF_F) { Obj* o = R[B].r; INST_CHECK(o); auto* in = static_cast<InstObj*>(o); FIELD_CHECK(in, C, K_F); R[A] = in->fields()[C]; DISPATCH(); }
  CASE(GETF_R) { Obj* o = R[B].r; INST_CHECK(o); auto* in = static_cast<InstObj*>(o); FIELD_CHECK(in, C, K_R); R[A] = in->fields()[C]; DISPATCH(); }
  // a write makes the property present again (records with a presence mask, D7)
#define MARK_PRESENT(in, idx) \
  if (E7_UNLIKELY((in)->cls->presence)) setFieldPresent(in, idx, true);
  CASE(SETF_I) { Obj* o = R[A].r; INST_CHECK(o); auto* in = static_cast<InstObj*>(o); FIELD_CHECK(in, B, K_I); in->fields()[B] = R[C]; MARK_PRESENT(in, B); DISPATCH(); }
  CASE(SETF_F) { Obj* o = R[A].r; INST_CHECK(o); auto* in = static_cast<InstObj*>(o); FIELD_CHECK(in, B, K_F); in->fields()[B] = R[C]; MARK_PRESENT(in, B); DISPATCH(); }
  CASE(SETF_R) { Obj* o = R[A].r; INST_CHECK(o); auto* in = static_cast<InstObj*>(o); FIELD_CHECK(in, B, K_R); in->fields()[B] = R[C]; MARK_PRESENT(in, B); DISPATCH(); }

#define IC_FIELD(in, slot, K, outIdx)                                                   \
  {                                                                                     \
    InlineCache& ic = fn->ics[slot];                                                    \
    if (E7_UNLIKELY(ic.cls != (in)->cls)) {                                             \
      std::string nm = utf16ToUtf8(M->m.strs[ic.name]);                                 \
      auto it = (in)->cls->fieldIndex.find(nm);                                         \
      if (it == (in)->cls->fieldIndex.end()) THROW("object has no property '%s'", nm.c_str()); \
      if (kindOfVT((in)->cls->fieldVT[it->second]) != (K)) THROW("property '%s' has unexpected type", nm.c_str()); \
      ic.cls = (in)->cls;                                                               \
      ic.index = it->second;                                                            \
    }                                                                                   \
    outIdx = ic.index;                                                                  \
  }
  CASE(GETN_I) CASE(GETN_F) CASE(GETN_R) {
    Obj* o = R[B].r;
    INST_CHECK(o);
    auto* in = static_cast<InstObj*>(o);
    uint16_t op = (uint16_t)ins;
    Kind k = op == OP_GETN_I ? K_I : op == OP_GETN_F ? K_F : K_R;
    uint32_t idx;
    IC_FIELD(in, C, k, idx);
    R[A] = in->fields()[idx];
    DISPATCH();
  }
  CASE(SETN_I) CASE(SETN_F) CASE(SETN_R) {
    Obj* o = R[A].r;
    INST_CHECK(o);
    auto* in = static_cast<InstObj*>(o);
    uint16_t op = (uint16_t)ins;
    Kind k = op == OP_SETN_I ? K_I : op == OP_SETN_F ? K_F : K_R;
    uint32_t idx;
    IC_FIELD(in, B, k, idx);
    in->fields()[idx] = R[C];
    MARK_PRESENT(in, idx);
    DISPATCH();
  }
  CASE(INSTOF) {
    Obj* o = R[B].r;
    bool res = false;
    if (o && o->type == OT::Instance) {
      ClassRT* target = &M->classes[C];
      for (ClassRT* c = static_cast<InstObj*>(o)->cls; c; c = c->parent)
        if (c == target) { res = true; break; }
    }
    R[A].i = res;
    DISPATCH();
  }

  // ---- boxes
  CASE(BOX_I) R[A].r = newBox((uint8_t)C, R[B]); DISPATCH();
  CASE(BOX_F) R[A].r = newBox(VT_NUMBER, R[B]); DISPATCH();
  CASE(UNBOX_I) {
    Obj* o = R[B].r;
    if (E7_UNLIKELY(!o || o->type != OT::Box || kindOfVT(static_cast<BoxObj*>(o)->vt) != K_I))
      THROW("unexpected null or undefined value");
    R[A] = static_cast<BoxObj*>(o)->v;
    DISPATCH();
  }
  CASE(UNBOX_F) {
    Obj* o = R[B].r;
    if (E7_UNLIKELY(!o || o->type != OT::Box || static_cast<BoxObj*>(o)->vt != VT_NUMBER))
      THROW("unexpected null or undefined value");
    R[A] = static_cast<BoxObj*>(o)->v;
    DISPATCH();
  }
  CASE(CHKNN) {
    Obj* o = R[A].r;
    if (E7_UNLIKELY(!o || o == undef_)) THROW("unexpected null or undefined value");
    DISPATCH();
  }

  // ---- arrays
  CASE(NEWARR) R[A].r = newArray((uint8_t)B, C); DISPATCH();
#define ARR_CHECK(o, K)                                                                 \
  if (E7_UNLIKELY(!(o) || (o)->type != OT::Array || kindOfVT(static_cast<ArrObj*>(o)->vt) != (K))) \
    THROW((o) ? "invalid array access" : "cannot index null or undefined");
#define FIDX(d, len, out)                                                               \
  {                                                                                     \
    double d_ = (d);                                                                    \
    if (E7_UNLIKELY(!(d_ >= 0 && d_ < (double)(len)) || (double)(uint32_t)d_ != d_))    \
      THROW("index %s out of range (length %u)", numberToString(d_).c_str(), (unsigned)(len)); \
    out = (uint32_t)d_;                                                                 \
  }
#define IIDX(v, len, out)                                                               \
  {                                                                                     \
    int64_t v_ = (v);                                                                   \
    if (E7_UNLIKELY(v_ < 0 || (uint64_t)v_ >= (uint64_t)(len)))                         \
      THROW("index %lld out of range (length %u)", (long long)v_, (unsigned)(len));     \
    out = (uint32_t)v_;                                                                 \
  }
#define AGET_IMPL(K, INDEXER)                                                           \
  {                                                                                     \
    Obj* o = R[B].r;                                                                    \
    ARR_CHECK(o, K);                                                                    \
    auto* a = static_cast<ArrObj*>(o);                                                  \
    uint32_t i;                                                                         \
    INDEXER(R[C], a->len, i);                                                           \
    R[A] = a->data[i];                                                                  \
    DISPATCH();                                                                         \
  }
#define FIDXS(s, len, out) FIDX((s).f, len, out)
#define IIDXS(s, len, out) IIDX((s).i, len, out)
  CASE(AGET_I) AGET_IMPL(K_I, FIDXS)
  CASE(AGET_F) AGET_IMPL(K_F, FIDXS)
  CASE(AGET_R) AGET_IMPL(K_R, FIDXS)
  CASE(AGETI_I) AGET_IMPL(K_I, IIDXS)
  CASE(AGETI_F) AGET_IMPL(K_F, IIDXS)
  CASE(AGETI_R) AGET_IMPL(K_R, IIDXS)
#define FIDXW(s, len, out) FIDX((s).f, (len) + 1, out)
#define IIDXW(s, len, out) IIDX((s).i, (len) + 1, out)
#define ASET_IMPL(K, INDEXER)                                                           \
  {                                                                                     \
    Obj* o = R[A].r;                                                                    \
    ARR_CHECK(o, K);                                                                    \
    auto* a = static_cast<ArrObj*>(o);                                                  \
    uint32_t i;                                                                         \
    INDEXER(R[B], a->len, i);                                                           \
    if (i == a->len) { fr->ip = ip; arrayPush(a, R[C]); }                               \
    else a->data[i] = R[C];                                                             \
    DISPATCH();                                                                         \
  }
  CASE(ASET_I) ASET_IMPL(K_I, FIDXW)
  CASE(ASET_F) ASET_IMPL(K_F, FIDXW)
  CASE(ASET_R) ASET_IMPL(K_R, FIDXW)
  CASE(ASETI_I) ASET_IMPL(K_I, IIDXW)
  CASE(ASETI_F) ASET_IMPL(K_F, IIDXW)
  CASE(ASETI_R) ASET_IMPL(K_R, IIDXW)
#define APUSH_IMPL(K)                                                                   \
  {                                                                                     \
    Obj* o = R[A].r;                                                                    \
    ARR_CHECK(o, K);                                                                    \
    fr->ip = ip;                                                                        \
    arrayPush(static_cast<ArrObj*>(o), R[B]);                                           \
    DISPATCH();                                                                         \
  }
  CASE(APUSH_I) APUSH_IMPL(K_I)
  CASE(APUSH_F) APUSH_IMPL(K_F)
  CASE(APUSH_R) APUSH_IMPL(K_R)
  CASE(ALEN) {
    Obj* o = R[B].r;
    if (E7_UNLIKELY(!o || o->type != OT::Array)) THROW("cannot read length of null or undefined");
    R[A].f = static_cast<ArrObj*>(o)->len;
    DISPATCH();
  }
  CASE(ALENI) {
    Obj* o = R[B].r;
    if (E7_UNLIKELY(!o || o->type != OT::Array)) THROW("cannot read length of null or undefined");
    R[A].i = static_cast<ArrObj*>(o)->len;
    DISPATCH();
  }

  // ---- bytes
#define BYTES_CHECK(o)                                                                  \
  if (E7_UNLIKELY(!(o) || (o)->type != OT::Bytes)) THROW((o) ? "value is not a byte array" : "cannot index null or undefined");
  CASE(BGET) { Obj* o = R[B].r; BYTES_CHECK(o); auto* b = static_cast<BytesObj*>(o); uint32_t i; FIDX(R[C].f, b->len, i); R[A].f = b->ptr()[i]; DISPATCH(); }
  CASE(BGETI) { Obj* o = R[B].r; BYTES_CHECK(o); auto* b = static_cast<BytesObj*>(o); uint32_t i; IIDX(R[C].i, b->len, i); R[A].i = b->ptr()[i]; DISPATCH(); }
  CASE(BSET) { Obj* o = R[A].r; BYTES_CHECK(o); auto* b = static_cast<BytesObj*>(o); uint32_t i; FIDX(R[B].f, b->len, i); b->ptr()[i] = (uint8_t)doubleToInt64Bits(R[C].f); DISPATCH(); }
  CASE(BSETI) { Obj* o = R[A].r; BYTES_CHECK(o); auto* b = static_cast<BytesObj*>(o); uint32_t i; IIDX(R[B].i, b->len, i); b->ptr()[i] = (uint8_t)R[C].u; DISPATCH(); }
  CASE(BLEN) { Obj* o = R[B].r; BYTES_CHECK(o); R[A].f = static_cast<BytesObj*>(o)->len; DISPATCH(); }
  CASE(BLENI) { Obj* o = R[B].r; BYTES_CHECK(o); R[A].i = static_cast<BytesObj*>(o)->len; DISPATCH(); }

  // ---- strings
#define STR_CHECK(o) \
  if (E7_UNLIKELY(!(o) || (o)->type != OT::String)) THROW((o) ? "value is not a string" : "unexpected null or undefined string");
  CASE(SLEN) { Obj* o = R[B].r; STR_CHECK(o); R[A].f = static_cast<StrObj*>(o)->len; DISPATCH(); }
  CASE(SCAT) {
    Obj* x = R[B].r;
    Obj* y = R[C].r;
    if (E7_UNLIKELY(x == undef_ || y == undef_)) {
      // a string-typed value that is undefined at run time (a regular expression group that did not
      // participate, typed string as TypeScript types it): "undefined", as in JavaScript
      fr->ip = ip;
      StrObj* u = newStringAscii("undefined");
      if (x == undef_) x = u;
      if (y == undef_) y = u;
      STR_CHECK(x);
      STR_CHECK(y);
      auto* ux = static_cast<StrObj*>(x);
      auto* uy = static_cast<StrObj*>(y);
      tempRoots_.push_back(u);  // the allocation below may collect (an error resets the temporary roots)
      StrObj* us = allocString((size_t)ux->len + uy->len);
      tempRoots_.pop_back();
      memcpy(us->chars(), ux->chars(), (size_t)ux->len * 2);
      memcpy(us->chars() + ux->len, uy->chars(), (size_t)uy->len * 2);
      R[A].r = us;
      DISPATCH();
    }
    STR_CHECK(x);
    STR_CHECK(y);
    auto* sx = static_cast<StrObj*>(x);
    auto* sy = static_cast<StrObj*>(y);
    fr->ip = ip;
    if (sy->len == 0) { R[A].r = sx; DISPATCH(); }
    if (sx->len == 0) { R[A].r = sy; DISPATCH(); }
    StrObj* s = allocString((size_t)sx->len + sy->len);
    memcpy(s->chars(), sx->chars(), (size_t)sx->len * 2);
    memcpy(s->chars() + sx->len, sy->chars(), (size_t)sy->len * 2);
    R[A].r = s;
    DISPATCH();
  }
  CASE(SEQ) CASE(SNE) {
    Obj* x = R[B].r;
    Obj* y = R[C].r;
    bool eq;
    if (x == y) eq = true;
    else if (!x || !y || x->type != OT::String || y->type != OT::String) eq = false;
    else eq = strEq(static_cast<StrObj*>(x), static_cast<StrObj*>(y));
    R[A].i = ((uint16_t)ins == OP_SEQ) ? eq : !eq;
    DISPATCH();
  }
  CASE(SLT) CASE(SLE) {
    Obj* x = R[B].r;
    Obj* y = R[C].r;
    STR_CHECK(x);
    STR_CHECK(y);
    int c = strCmp(static_cast<StrObj*>(x), static_cast<StrObj*>(y));
    R[A].i = ((uint16_t)ins == OP_SLT) ? c < 0 : c <= 0;
    DISPATCH();
  }
  CASE(NUM2STR) { fr->ip = ip; R[A].r = numToStringObj(R[B].f); DISPATCH(); }
  CASE(INT2STR) {
    fr->ip = ip;
    R[A].r = intToStringObj(R[B], (uint8_t)C);
    DISPATCH();
  }
  CASE(BOOL2STR) { fr->ip = ip; R[A].r = newStringAscii(R[B].i ? "true" : "false"); DISPATCH(); }
  CASE(STRTRUTHY) {
    Obj* o = R[B].r;
    R[A].i = o && o->type == OT::String && static_cast<StrObj*>(o)->len > 0;
    DISPATCH();
  }
  CASE(EQ_R) R[A].i = R[B].r == R[C].r; DISPATCH();
  CASE(NE_R) R[A].i = R[B].r != R[C].r; DISPATCH();
  CASE(ABORT) {
    Obj* o = R[A].r;
    std::string msg = (o && o->type == OT::String) ? strUtf8(static_cast<StrObj*>(o)) : "abort";
    fr->ip = ip;
    raise(ERR_RUNTIME, msg, false);  // abort() and compiler-inserted checks are never catchable
  }
  CASE(THROW) {
    fr->ip = ip;
    raiseThrown(R[A].r);
  }
  // `debugger;`: the debug interpreter has already stopped here (DF_DEBUGGER) when appropriate
  CASE(DEBUGGER) DISPATCH();

  // ---- coroutines (DESIGN.md D1)
  // first instruction of a generator: save the frame (arguments in place) into a new coroutine and
  // return it without running the body
  CASE(GEN_START) {
    fr->ip = ip;
    CoroObj* co = newCoro(fn, fr->closure);  // may collect: the frame's registers are roots
    R = stack_ + fr->base;
    co->pc = (uint32_t)(ip - fn->code);
    memcpy(co->regs(), R, (size_t)fn->nregs * sizeof(Slot));
    co->state = CO_START;
    Slot cv;
    cv.r = co;
    DO_RET(cv, 'R');
  }
  CASE(YIELD) {
    ip++;  // the extra word (read on resumption)
    CoroObj* co = fr->coro;
    if (E7_UNLIKELY(!co)) THROW("yield outside a running generator");
    if (E7_UNLIKELY(fflagsKind(co->fflags) == FK_ASYNC_GENERATOR)) {
      // AsyncGeneratorYield (the value is already awaited): the first request gets
      // { value, done: false }; a request queued after it is answered here at once, else the
      // generator suspends
      fr->ip = ip;
      agenCompleteStep(co, false, R[A], fflagsYield(co->fflags), false);  // may collect: the frame is in place
      R = stack_ + fr->base;
      if (co->queue && !co->queue->empty()) {
        const AGenRequest& q = co->queue->front();
        uint16_t recv = B, retReg = (uint16_t)ip[-1];
        R[C].i = q.mode;
        uint16_t into = q.mode == 0 ? recv : q.mode == 2 ? retReg : kNoReg;
        if (into != kNoReg) {
          if (q.kind != 0) R[into] = q.value;
          else {
            uint8_t k = q.mode == 0 ? fflagsNext(co->fflags) : fflagsReturn(co->fflags);
            if (k == 3) R[into].r = undef_;
            else if (k == 2) R[into].f = std::nan("");
            else R[into].i = 0;
          }
        }
        if (q.mode == 1) raiseThrown(q.value.r);  // at the YIELD (its handlers apply)
        DISPATCH();
      }
      co->pc = (uint32_t)(ip - 2 - fn->code);
      memcpy(co->regs(), R, (size_t)fn->nregs * sizeof(Slot));
      co->state = CO_SUSPENDED;
      bool ent = fr->entry;
      frames_.pop_back();
      if (DBG && E7_UNLIKELY(frames_.size() < dbgRetDepth_)) dbgMask_.fetch_or(DF_ALWAYS);
      if (ent) { resultKind_ = 'V'; return ERR_OK; }
      LOAD_FRAME();
      DISPATCH();
    }
    Slot v = R[A];
    co->pc = (uint32_t)(ip - 2 - fn->code);
    memcpy(co->regs(), R, (size_t)fn->nregs * sizeof(Slot));
    co->state = CO_SUSPENDED;
    bool ent = fr->entry;
    uint16_t dst = fr->retDst, dd = fr->doneDst;
    frames_.pop_back();
    if (DBG && E7_UNLIKELY(frames_.size() < dbgRetDepth_)) dbgMask_.fetch_or(DF_ALWAYS);
    if (ent) { result_ = v; resultKind_ = 'R'; return ERR_OK; }
    LOAD_FRAME();
    if (dst != kNoReg) R[dst] = v;
    if (dd != kNoReg) R[dd].i = 0;
    DISPATCH();
  }
  // resume a generator: A coroutine, B its value (kind of the variant), C the done flag; the extra
  // word: bits 0-15 the sent value's register, 16-17 the mode (0 next, 1 throw, 2 return), 18-19 the
  // sent value's kind (0 none, 1 I, 2 F, 3 R)
  CASE(RESUME_I) CASE(RESUME_F) CASE(RESUME_R) {
    uint64_t ex = *ip++;
    uint16_t sent = (uint16_t)ex;
    uint32_t mode = (uint32_t)(ex >> 16) & 3, sentKind = (uint32_t)(ex >> 18) & 3;
    uint16_t op = (uint16_t)ins;
    uint8_t want = op == OP_RESUME_I ? 1 : op == OP_RESUME_F ? 2 : 3;
    Obj* o = R[A].r;
    if (E7_UNLIKELY(!o || o->type != OT::Coro)) THROW(o ? "value is not a generator" : "cannot resume null or undefined");
    auto* co = static_cast<CoroObj*>(o);
    uint8_t ff = co->fflags;
    uint16_t vdst = B, ddst = C;
    if (E7_UNLIKELY((ex & kResumeAsync) != 0)) {
      // a request to an async generator: B receives its promise; the body runs now only when
      // it waits at its start or at a yield (AsyncGeneratorResume)
      if (E7_UNLIKELY(!isAsyncGen(co))) THROW("generator type mismatch");
      fr->ip = ip;
      Slot sv;
      sv.u = 0;
      if (sentKind != 0) sv = R[sent];
      bool run = false;
      PromiseObj* p = agenRequest(co, mode, sv, (uint8_t)sentKind, &M->classes[resumeClass(ex)], resumeElemVT(ex), &run);
      R = stack_ + fr->base;
      R[B].r = p;
      R[C].i = 0;
      if (!run) DISPATCH();
      vdst = ddst = kNoReg;  // the generator gives its caller nothing when it yields, awaits or returns
    } else {
      if (E7_UNLIKELY(fflagsKind(ff) != FK_GENERATOR || fflagsYield(ff) != want)) THROW("generator type mismatch");
      uint8_t expect = mode == 0 ? fflagsNext(ff) : mode == 2 ? fflagsReturn(ff) : 3;
      if (E7_UNLIKELY(sentKind != 0 && sentKind != expect)) THROW("generator type mismatch");
      if (E7_UNLIKELY(co->state == CO_RUNNING)) THROW("TypeError: Generator is already running");
      if (co->state == CO_DONE || (co->state == CO_START && mode != 0)) {
        // not started (return / throw finish it without running the body) or already finished
        co->state = CO_DONE;
        if (mode == 1) { fr->ip = ip; raiseThrown(R[sent].r); }
        if (mode == 2 && sentKind == want) R[B] = R[sent];
        else if (fflagsReturn(ff)) {
          // a finished generator gives undefined (a number NaN, an integer 0: the register's kind is fixed)
          if (want == 3) R[B].r = undef_;
          else if (want == 2) R[B].f = std::nan("");
          else R[B].i = 0;
        }
        R[C].i = 1;
        DISPATCH();
      }
    }
    FuncRT* cf = co->fn;
    uint32_t nb = fr->base + fn->nregs;
    if (E7_UNLIKELY((size_t)nb + cf->nregs > cfg_.stackSlots || frames_.size() >= cfg_.maxFrames))
      THROWC(ERR_STACK, "stack overflow");
    Slot* NR = stack_ + nb;
    memcpy(NR, co->regs(), (size_t)cf->nregs * sizeof(Slot));
    const uint64_t* cip = cf->code + co->pc;
    Obj* thrown = nullptr;
    if (co->state == CO_SUSPENDED) {
      uint64_t yw = cip[0], yx = cip[1];
      uint16_t recv = insB(yw), modeReg = insC(yw), retReg = (uint16_t)yx;
      NR[modeReg].i = mode;
      uint16_t into = mode == 0 ? recv : mode == 2 ? retReg : kNoReg;
      if (into != kNoReg) {
        if (sentKind != 0) NR[into] = R[sent];
        else {
          uint8_t k = mode == 0 ? fflagsNext(ff) : fflagsReturn(ff);
          if (k == 3) NR[into].r = undef_;
          else if (k == 2) NR[into].f = std::nan("");
          else NR[into].i = 0;
        }
      }
      if (mode == 1) thrown = R[sent].r;
      cip += 2;
    }
    co->state = CO_RUNNING;
    fr->ip = ip;
    frames_.push_back(Frame{cf, cip, co->closure, nb, vdst, false, co, ddst});
    LOAD_FRAME();
    if (mode == 1) {
      budget_ -= 1;
      raiseThrown(thrown);  // raised at the YIELD (its handlers apply)
    }
    TICK(1);
    DISPATCH();
  }
  // D7: field B of the record in A is not an own property (an object literal leaves it out)
  CASE(FIELD_ABSENT) {
    Obj* o = R[A].r;
    INST_CHECK(o);
    auto* in = static_cast<InstObj*>(o);
    if (E7_UNLIKELY(!in->cls->presence || B >= in->cls->nfields)) THROW("invalid field access");
    setFieldPresent(in, B, false);
    DISPATCH();
  }
  // first instruction of an async function: its coroutine (owning this frame) and its promise, of
  // value type A; the body runs on until the first await
  CASE(ASYNC_START) {
    fr->ip = ip;
    CoroObj* co = newCoro(fn, fr->closure);
    co->state = CO_RUNNING;
    fr->coro = co;  // rooted by the frame
    co->promise = newPromise((uint8_t)A);
    DISPATCH();
  }
  // await the promise in B; the value goes to A (or the rejection is raised here) when a job resumes
  // the function. Leaving the frame gives the caller the function's promise (first await) or ends
  // the job.
  CASE(AWAIT) {
    CoroObj* co = fr->coro;
    // an async function (its promise goes to the caller at the first await) or an async generator
    // (its caller has the request's promise already)
    if (E7_UNLIKELY(!co || !((fflagsKind(co->fflags) == FK_ASYNC && co->promise) || fflagsKind(co->fflags) == FK_ASYNC_GENERATOR)))
      THROW("await outside a running async function");
    Obj* po = R[B].r;
    if (E7_UNLIKELY(!po || po->type != OT::Promise)) THROW("await: the value is not a promise");
    auto* awaited = static_cast<PromiseObj*>(po);
    if (E7_UNLIKELY(A != kNoReg && kindOfVT(awaited->vt) != fn->d->regKinds[A])) THROW("await: promise value type mismatch");
    fr->ip = ip;
    co->pc = (uint32_t)(ip - 1 - fn->code);
    memcpy(co->regs(), R, (size_t)fn->nregs * sizeof(Slot));
    co->state = CO_SUSPENDED;
    Reaction rr;
    rr.kind = RK_AWAIT;
    rr.a = co;
    promiseThen(awaited, rr);
    PromiseObj* pr = co->promise;
    bool ent = fr->entry;
    uint16_t dst = fr->retDst;
    frames_.pop_back();
    if (DBG && E7_UNLIKELY(frames_.size() < dbgRetDepth_)) dbgMask_.fetch_or(DF_ALWAYS);
    if (ent) {
      if (pr) { result_.r = pr; resultKind_ = 'R'; }
      else resultKind_ = 'V';
      return ERR_OK;
    }
    LOAD_FRAME();
    if (dst != kNoReg && pr) R[dst].r = pr;
    DISPATCH();
  }

#if !E7_COMPUTED_GOTO
  default:
    THROW("invalid opcode");
  }
#endif
  return ERR_RUNTIME;  // unreachable
#undef DBG_HOOK
#undef DISPATCH
#undef CASE
}

