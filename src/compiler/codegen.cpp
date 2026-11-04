// Code generation: typed AST -> e7vm register bytecode.
#include "codegen.h"

#include <cmath>
#include <cstring>
#include <map>

#include "intrinsics.h"

namespace e7 {

namespace {

Expr* strip(Expr* e) {
  while (e && e->k == EK::Paren) e = e->a;
  return e;
}

struct Label {
  std::vector<size_t> patches;     // bx absolute targets
  std::vector<size_t> relPatches;  // 16-bit relative targets (fused branches)
  int64_t target = -1;
};

struct LoopCtx {
  std::string label;
  Label* brk;
  Label* cont;  // null for switch / labeled blocks
  bool isLoop;
  bool isSwitch = false;
};

struct Gen {
 public:
  Gen(Program& p, Checker& c, Module& m) : P(p), C(c), T(p.types), M(m) {}

  bool run(const std::string& name);

  Program& P;
  Checker& C;
  Types& T;
  Module& M;
  std::map<int64_t, uint32_t> intIdx;
  std::map<uint64_t, uint32_t> numIdx;
  std::map<std::u16string, uint32_t> strIdx;
  std::map<std::string, uint32_t> sigIdx;
  std::map<std::string, uint32_t> nativeIdx;

  // per-function state
  FuncSym* fn = nullptr;
  FuncDesc* D = nullptr;
  std::vector<uint16_t> freeRegs[3];
  std::vector<uint16_t> temps;
  std::unordered_map<VarSym*, uint16_t> varReg;
  std::vector<LoopCtx> loops;
  std::vector<std::unique_ptr<Label>> labels;
  std::string pendingLabel;
  uint32_t curLine = 0;
  std::string curFile;
  std::vector<Stmt*> pendingCtorInits;
  bool emittedFieldInits = false;
  uint16_t argcReg = kNoReg;

  // ---------------------------------------------------------------- errors
  void err(Loc l, const std::string& msg) { P.errors.push_back(CompileError{curFile, l, msg}); }

  // ---------------------------------------------------------------- constants
  uint32_t kInt(int64_t v) {
    auto it = intIdx.find(v);
    if (it != intIdx.end()) return it->second;
    uint32_t i = (uint32_t)M.ints.size();
    M.ints.push_back(v);
    return intIdx[v] = i;
  }
  uint32_t kNum(double d) {
    uint64_t b;
    memcpy(&b, &d, 8);
    auto it = numIdx.find(b);
    if (it != numIdx.end()) return it->second;
    uint32_t i = (uint32_t)M.nums.size();
    M.nums.push_back(d);
    return numIdx[b] = i;
  }
  uint32_t kStr(const std::u16string& s) {
    auto it = strIdx.find(s);
    if (it != strIdx.end()) return it->second;
    uint32_t i = (uint32_t)M.strs.size();
    M.strs.push_back(s);
    return strIdx[s] = i;
  }
  uint32_t sig(const std::string& s) {
    auto it = sigIdx.find(s);
    if (it != sigIdx.end()) return it->second;
    uint32_t i = (uint32_t)M.sigs.size();
    M.sigs.push_back(s);
    return sigIdx[s] = i;
  }
  uint32_t native(const std::string& name, const std::string& sg) {
    std::string key = name + "|" + sg;
    auto it = nativeIdx.find(key);
    if (it != nativeIdx.end()) return it->second;
    uint32_t i = (uint32_t)M.natives.size();
    M.natives.push_back(NativeImport{name, sig(sg)});
    return nativeIdx[key] = i;
  }

  // ---------------------------------------------------------------- emission
  size_t pc() const { return D->code.size(); }
  void line(Loc l) {
    if (l.line && l.line != curLine) {
      curLine = l.line;
      D->lines.push_back((uint32_t)pc());
      D->lines.push_back(l.line);
    }
  }
  void emit(uint16_t op, uint16_t a = 0, uint16_t b = 0, uint16_t c = 0) { D->code.push_back(encode(op, a, b, c)); }
  void emitBx(uint16_t op, uint16_t a, uint32_t bx) { D->code.push_back(encodeBx(op, a, bx)); }
  void emitWord(uint64_t w) { D->code.push_back(w); }
  void emitRegList(const std::vector<uint16_t>& regs) {
    for (size_t i = 0; i < regs.size(); i += 4) {
      uint64_t w = 0;
      for (size_t k = 0; k < 4 && i + k < regs.size(); k++) w |= (uint64_t)regs[i + k] << (16 * k);
      emitWord(w);
    }
  }
  Label* newLabel() {
    labels.emplace_back(new Label());
    return labels.back().get();
  }
  void jump(uint16_t op, uint16_t a, Label* l) {
    if (l->target >= 0) emitBx(op, a, (uint32_t)l->target);
    else {
      l->patches.push_back(pc());
      emitBx(op, a, 0);
    }
  }
  void place(Label* l) {
    l->target = (int64_t)pc();
    for (size_t p : l->patches) D->code[p] = (D->code[p] & 0xFFFFFFFFull) | ((uint64_t)(uint32_t)l->target << 32);
    for (size_t p : l->relPatches) {
      int64_t off = l->target - (int64_t)(p + 1);
      if (off < INT16_MIN || off > INT16_MAX) relOverflow = true;
      D->code[p] = (D->code[p] & 0x0000FFFFFFFFFFFFull) | ((uint64_t)(uint16_t)(int16_t)off << 48);
    }
    l->patches.clear();
    l->relPatches.clear();
  }
  // fused compare-and-branch: if (cond(a, b)) goto l
  void jumpRel(uint16_t op, uint16_t a, uint16_t b, Label* l) {
    if (l->target >= 0) {
      int64_t off = l->target - (int64_t)(pc() + 1);
      if (off < INT16_MIN || off > INT16_MAX) relOverflow = true;
      emit(op, a, b, (uint16_t)(int16_t)off);
    } else {
      l->relPatches.push_back(pc());
      emit(op, a, b, 0);
    }
  }
  bool relOverflow = false;
  bool allowFused = true;
  std::map<uint64_t, uint16_t> constRegs;  // hoisted number constants
  uint16_t numConst(double v) {
    uint64_t b;
    memcpy(&b, &v, 8);
    auto it = constRegs.find(b);
    if (it != constRegs.end()) return it->second;
    uint16_t r = newReg(K_F);
    constRegs[b] = r;
    return r;
  }
  // Prepend constant loads to the function and relocate jump targets / line info.
  void finishConstants() {
    if (constRegs.empty()) return;
    std::vector<uint64_t> prefix;
    for (auto& kv : constRegs) {
      double d;
      memcpy(&d, &kv.first, 8);
      prefix.push_back(encodeBx(OP_LOADK_F, kv.second, kNum(d)));
    }
    size_t k = prefix.size();
    std::vector<uint64_t>& code = D->code;
    size_t pcI = 0;
    while (pcI < code.size()) {
      uint64_t w = code[pcI];
      uint16_t op = insOp(w);
      const OpInfo& oi = kOpInfo[op];
      size_t next = pcI + 1;
      if (oi.fmt[0] == '*') {
        uint16_t c = insC(w);
        bool meta = op == OP_CALLV || op == OP_CALLM || op == OP_CALLI;
        next += (meta ? 1 : 0) + (c + 3) / 4;
      } else if (oi.fmt[2] == '*' && oi.fmt[1] == 'J') {
        code[pcI] = (w & 0xFFFFFFFFull) | ((uint64_t)(insBx(w) + (uint32_t)k) << 32);
      }
      pcI = next;
    }
    code.insert(code.begin(), prefix.begin(), prefix.end());
    for (size_t i = 0; i + 1 < D->lines.size(); i += 2) D->lines[i] += (uint32_t)k;
  }

  // ---------------------------------------------------------------- registers
  uint16_t newReg(Kind k) {
    if (D->regKinds.size() >= 65000) {
      err(Loc(), "function '" + fn->name + "' needs too many registers");
      return 0;
    }
    D->regKinds.push_back((uint8_t)k);
    return (uint16_t)(D->regKinds.size() - 1);
  }
  uint16_t allocReg(Kind k) {
    if (k == K_V) k = K_I;
    auto& fl = freeRegs[k];
    if (!fl.empty()) {
      uint16_t r = fl.back();
      fl.pop_back();
      return r;
    }
    return newReg(k);
  }
  void freeReg(uint16_t r) {
    if (r == kNoReg) return;
    freeRegs[D->regKinds[r]].push_back(r);
  }
  uint16_t temp(Kind k) {
    uint16_t r = allocReg(k);
    temps.push_back(r);
    return r;
  }
  size_t mark() const { return temps.size(); }
  void release(size_t m) {
    while (temps.size() > m) {
      freeReg(temps.back());
      temps.pop_back();
    }
  }
  Kind kindOfReg(uint16_t r) { return (Kind)D->regKinds[r]; }

  // ---------------------------------------------------------------- helpers
  static Kind K(Type* t) {
    Kind k = kindOf(t);
    return k == K_V ? K_I : k;
  }
  static std::string kindSuffixOf(Type* t) { return std::string(1, kindChar(K(t))); }
  std::string jsonDescriptor(Type* t);
  void loadInt(uint16_t dst, int64_t v) {
    if (v >= INT32_MIN && v <= INT32_MAX) emitBx(OP_LOADI, dst, (uint32_t)(int32_t)v);
    else emitBx(OP_LOADK_I, dst, kInt(v));
  }
  void loadNum(uint16_t dst, double v) { emitBx(OP_LOADK_F, dst, kNum(v)); }
  void loadStr(uint16_t dst, const std::u16string& s) { emitBx(OP_LOADK_S, dst, kStr(s)); }
  void loadZero(uint16_t dst, Kind k) {
    if (k == K_I) loadInt(dst, 0);
    else if (k == K_F) loadNum(dst, 0);
    else emit(OP_LOADNULL, dst);
  }
  void mov(uint16_t dst, uint16_t src, Kind k) {
    if (dst == src) return;
    static const uint16_t ops[3] = {OP_MOV_I, OP_MOV_F, OP_MOV_R};
    emit(ops[k], dst, src);
  }
  uint16_t op3(Kind k, uint16_t i, uint16_t f, uint16_t r) { return k == K_I ? i : k == K_F ? f : r; }

  void wrapInt(uint16_t dst, uint16_t src, Type* t) {
    switch (t->vt) {
      case VT_I8: emit(OP_WRAP_I8, dst, src); break;
      case VT_U8: emit(OP_WRAP_U8, dst, src); break;
      case VT_I16: emit(OP_WRAP_I16, dst, src); break;
      case VT_U16: emit(OP_WRAP_U16, dst, src); break;
      case VT_I32: emit(OP_WRAP_I32, dst, src); break;
      case VT_U32: emit(OP_WRAP_U32, dst, src); break;
      default: if (dst != src) emit(OP_MOV_I, dst, src); break;
    }
  }
  void intToNum(uint16_t dst, uint16_t src, Type* t) { emit(t->t == TT::Int && t->vt == VT_U64 ? OP_U2F : OP_I2F, dst, src); }

  // native call: args are registers; result kind from ret ('V' for void)
  void callNative(const std::string& name, const std::vector<uint16_t>& args, uint16_t dst, char ret) {
    std::string sg;
    for (uint16_t a : args) sg += kindChar(kindOfReg(a));
    sg += '>';
    sg += ret;
    uint32_t ni = native(name, sg);
    if (args.size() > 65535) { err(Loc(), "too many arguments"); return; }
    emit(OP_CALLN, ret == 'V' ? kNoReg : dst, (uint16_t)ni, (uint16_t)args.size());
    emitRegList(args);
  }

  // An expression node that reads an already-evaluated register.
  Expr* regExpr(uint16_t reg, Type* t, Loc l) {
    VarSym* v = P.make<VarSym>();
    v->name = "<tmp>";
    v->type = t;
    v->owner = fn;
    varReg[v] = reg;
    Expr* id = P.arena.expr(EK::Ident, l);
    id->var = v;
    id->type = t;
    return id;
  }

  // ---------------------------------------------------------------- program
  void genFunction(FuncSym* f);
  void genFunctionOnce(FuncSym* f);
  void genInit();
  void genInitOnce();
  void prologue(FuncSym* f);
  void genCtorFieldInits();

  // statements
  void genStmt(Stmt* s);
  void genBlock(std::vector<Stmt*>& stmts);
  void blockEntry(std::vector<Stmt*>& stmts);
  void declareLocal(VarSym* v);
  void bindVar(VarSym* v, uint16_t valueReg);
  void genVarDecl(VarDecl& d, bool topLevel);
  void destructure(VarDecl& d, uint16_t src, Type* t);
  void genForOf(Stmt* s);
  void genSwitch(Stmt* s);
  LoopCtx* findLoop(const std::string& label, bool forContinue);

  // expressions
  uint16_t gen(Expr* e);
  void genInto(Expr* e, uint16_t dst);
  void genDiscard(Expr* e);
  uint16_t genTruthy(Expr* e);
  void truthyOf(uint16_t dst, uint16_t src, Type* t);
  void condJump(Expr* e, bool jumpIf, Label* target);
  uint16_t toStringReg(uint16_t src, Type* t);
  void readVar(VarSym* v, uint16_t dst, Type* asType);
  uint16_t varLocation(VarSym* v, bool& isCell, bool& isUp, int& upIdx);
  void writeVar(VarSym* v, uint16_t src);
  void genAssign(Expr* e, uint16_t dst);
  void storeTo(Expr* target, uint16_t valueReg);
  void genBinary(Expr* e, uint16_t dst);
  void binaryOp(const std::string& op, Type* lt, Type* rt, uint16_t dst, uint16_t a, uint16_t b, Loc l);
  void genEquality(Expr* e, uint16_t dst, bool negate);
  void genCall(Expr* e, uint16_t dst);
  void genNew(Expr* e, uint16_t dst);
  void genMember(Expr* e, uint16_t dst);
  void genIndex(Expr* e, uint16_t dst);
  void genClosure(FuncSym* f, uint16_t dst);
  void genArrayLit(Expr* e, uint16_t dst);
  void genObjectLit(Expr* e, uint16_t dst);
  void genConsoleLog(Expr* e);
  void genCallbackLoop(Expr* e, uint16_t dst);
  void callFunc(FuncSym* f, uint16_t dst, const std::vector<uint16_t>& args, Type* ft, std::vector<Expr*>* argExprs);
  std::vector<uint16_t> genArgs(std::vector<Expr*>& args, Type* ft, bool restPacked, size_t* nGiven);
  void callClosure(uint16_t clo, Type* ft, const std::vector<uint16_t>& args, uint16_t dst);
  void boxIfNeeded(uint16_t dst, uint16_t src, Type* from, Type* to);
  void unboxNarrowed(uint16_t dst, uint16_t src, Type* storage, Type* want);
  uint8_t keyModeOf(Type* t);
  void spreadInto(uint16_t arr, Expr* sp, Type* elemT);
  void iterateCollection(Type* t, uint16_t coll, int mode, std::function<void(uint16_t idxOrElem, uint16_t second)> body, Label* brk, Label* cont);
};

// ======================================================================= helpers
uint8_t Gen::keyModeOf(Type* t) {
  switch (t->t) {
    case TT::String: return KM_STR;
    case TT::Number: return KM_NUM;
    case TT::Int: case TT::Bool: return KM_INT;
    case TT::BigInt: return KM_BIG;
    case TT::Enum: return t->en->isString ? KM_STR : KM_NUM;
    default: return KM_REF;
  }
}

void Gen::boxIfNeeded(uint16_t dst, uint16_t src, Type* from, Type* to) {
  if (to->t == TT::Optional && from->t != TT::Optional && kindOf(from) != K_R) {
    if (kindOf(from) == K_F) emit(OP_BOX_F, dst, src);
    else emit(OP_BOX_I, dst, src, vtOf(from));
  } else {
    mov(dst, src, K(to));
  }
}

void Gen::unboxNarrowed(uint16_t dst, uint16_t src, Type* storage, Type* want) {
  if (isBoxedOptional(storage) && want->t != TT::Optional && kindOf(want) != K_R) {
    emit(kindOf(want) == K_F ? OP_UNBOX_F : OP_UNBOX_I, dst, src);
  } else {
    mov(dst, src, K(want));
  }
}

// ======================================================================= program
bool Gen::run(const std::string& name) {
  M.name = name;
  // function indices (natives are not functions)
  uint32_t idx = 0;
  for (FuncSym* f : P.funcs) {
    if (f->isNative) continue;
    f->funcIdx = (int)idx++;
  }
  M.funcs.resize(idx);
  // globals
  for (VarSym* g : P.globals) {
    GlobalDesc gd;
    gd.name = g->name;
    gd.vt = g->type ? vtOf(g->type) : VT_REF;
    if (g->type && g->type->t == TT::Unknown) gd.vt = VT_REF;
    M.globals.push_back(gd);
  }
  for (FuncSym* f : P.funcs) {
    if (f->isNative) continue;
    if (f->isInit) genInit();
    else genFunction(f);
  }
  M.initFunc = (uint32_t)P.init->funcIdx;
  // classes (after functions so that records created during codegen are included)
  for (ClassSym* c : P.classes) {
    ClassDesc cd;
    cd.name = c->name;
    cd.parent = c->parent ? c->parent->classIdx : -1;
    cd.isRecord = c->isTuple ? 2 : c->isRecord ? 1 : 0;
    for (auto& f : c->fields) cd.fields.push_back(FieldDesc{f.name, vtOf(f.type)});
    for (auto& m : c->vtable) {
      cd.vtable.push_back((uint32_t)m.fn->funcIdx);
      cd.methodNames.push_back(m.name);
    }
    M.classes.push_back(cd);
  }
  // exports
  for (auto& ex : P.exports) {
    ExportDesc ed;
    ed.name = ex.first;
    ed.func = (uint32_t)ex.second->funcIdx;
    auto code = [](Type* t) -> char {
      switch (t->t) {
        case TT::Number: return 'n';
        case TT::Bool: return 'b';
        case TT::String: return 's';
        case TT::Bytes: return 'y';
        case TT::Handle: return 'h';
        case TT::Void: return 'v';
        case TT::Int: return (char)('1' + (t->vt - VT_I8));
        default: return 'x';
      }
    };
    Type* ft = ex.second->type;
    for (Type* p : ft->params) ed.hostSig += code(p);
    if (ex.second->hasDefaults) ed.hostSig += 'x';
    ed.hostSig += '>';
    ed.hostSig += code(ft->ret);
    M.exports.push_back(ed);
  }
  return P.errors.empty();
}

static std::string sigOf(FuncSym* f) {
  std::string s;
  if (f->isMethod || f->isCtor) s += 'R';
  for (Type* p : f->type->params) s += kindChar(Gen::K(p));
  if (f->hasDefaults) s += 'I';
  s += '>';
  Kind rk = kindOf(f->type->ret);
  s += rk == K_V ? 'V' : kindChar(rk);
  return s;
}

void Gen::genInit() {
  allowFused = true;
  genInitOnce();
  if (relOverflow) {
    allowFused = false;
    M.funcs[P.init->funcIdx] = FuncDesc();
    genInitOnce();
  }
  finishConstants();
}

void Gen::genInitOnce() {
  relOverflow = false;
  constRegs.clear();
  labels.clear();
  FuncSym* f = P.init;
  fn = f;
  D = &M.funcs[f->funcIdx];
  D->name = "<init>";
  D->sig = sig(">V");
  D->nparams = 0;
  for (auto& fl : freeRegs) fl.clear();
  temps.clear();
  varReg.clear();
  loops.clear();
  curLine = 0;
  for (auto& file : P.files) {
    curFile = file->path;
    blockEntry(file->body);
    for (Stmt* s : file->body) genStmt(s);
  }
  emit(OP_RET_V);
}

void Gen::prologue(FuncSym* f) {
  // parameters occupy the first registers
  uint16_t r = 0;
  if (f->isMethod || f->isCtor) {
    newReg(K_R);
    varReg[f->thisVar] = r++;
  }
  for (size_t i = 0; i < f->type->params.size(); i++) {
    newReg(K(f->type->params[i]));
    if (i < f->paramSyms.size()) varReg[f->paramSyms[i]] = r;
    r++;
  }
  if (f->hasDefaults) {
    argcReg = newReg(K_I);
    r++;
  }
  D->nparams = r;
  // default parameter values
  if (f->hasDefaults && f->params) {
    for (size_t i = 0; i < f->params->size(); i++) {
      Param& p = (*f->params)[i];
      if (!p.init) continue;
      Label* skip = newLabel();
      size_t m0 = mark();
      uint16_t lim = temp(K_I), cmp = temp(K_I);
      loadInt(lim, (int64_t)i);
      emit(OP_LT_I, cmp, lim, argcReg);  // i < argc: argument was given
      jump(OP_JT, cmp, skip);
      genInto(p.init, varReg[f->paramSyms[i]]);
      place(skip);
      release(m0);
    }
  }
  // captured parameters / this move into cells
  std::vector<VarSym*> ps = f->paramSyms;
  if (f->thisVar && (f->isMethod || f->isCtor)) ps.insert(ps.begin(), f->thisVar);
  for (VarSym* v : ps) {
    if (!v->captured) continue;
    uint16_t pr = varReg[v];
    uint16_t cell = allocReg(K_R);
    emit(OP_NEWCELL, cell, vtOf(v->type));
    static const uint16_t setOps[3] = {OP_CSET_I, OP_CSET_F, OP_CSET_R};
    emit(setOps[K(v->type)], cell, pr);
    varReg[v] = cell;
  }
}

void Gen::genCtorFieldInits() {
  if (emittedFieldInits) return;
  emittedFieldInits = true;
  ClassSym* c = fn->cls;
  size_t m0 = mark();
  uint16_t th = temp(K_R);
  readVar(fn->thisVar, th, fn->thisVar->type);
  // parameter properties
  if (fn->params) {
    for (size_t i = 0; i < fn->params->size(); i++) {
      Param& p = (*fn->params)[i];
      if (!p.isProperty) continue;
      int fi = c->fieldIdx[p.name];
      uint16_t v = temp(K(c->fields[fi].type));
      readVar(fn->paramSyms[i], v, fn->paramSyms[i]->type);
      static const uint16_t ops[3] = {OP_SETF_I, OP_SETF_F, OP_SETF_R};
      emit(ops[K(c->fields[fi].type)], th, (uint16_t)fi, v);
    }
  }
  for (size_t fi = 0; fi < c->fields.size(); fi++) {
    auto& fd = c->fields[fi];
    if (fd.owner != c) continue;
    Kind k = K(fd.type);
    static const uint16_t ops[3] = {OP_SETF_I, OP_SETF_F, OP_SETF_R};
    if (fd.init) {
      uint16_t v = temp(k);
      genInto(fd.init, v);
      emit(ops[k], th, (uint16_t)fi, v);
    } else if (fd.optional || (fd.type->t == TT::Optional && fd.type->hasUndef)) {
      uint16_t v = temp(K_R);
      emit(OP_LOADUNDEF, v);
      emit(ops[k], th, (uint16_t)fi, v);
    }
  }
  release(m0);
}

void Gen::genFunction(FuncSym* f) {
  allowFused = true;
  genFunctionOnce(f);
  if (relOverflow) {
    allowFused = false;
    M.funcs[f->funcIdx] = FuncDesc();
    genFunctionOnce(f);
  }
  finishConstants();
}

void Gen::genFunctionOnce(FuncSym* f) {
  relOverflow = false;
  constRegs.clear();
  labels.clear();
  fn = f;
  D = &M.funcs[f->funcIdx];
  D->name = f->qualName.empty() ? f->name : f->qualName;
  D->sig = sig(sigOf(f));
  for (auto& fl : freeRegs) fl.clear();
  temps.clear();
  varReg.clear();
  loops.clear();
  curLine = 0;
  curFile = f->file;
  argcReg = kNoReg;
  emittedFieldInits = false;
  for (VarSym* v : f->captures) D->upvalKinds.push_back((uint8_t)K_R);
  // upvalue kinds describe the *content* of the captured cells
  for (size_t i = 0; i < f->captures.size(); i++) D->upvalKinds[i] = (uint8_t)K(f->captures[i]->type);
  Loc start = f->declStmt ? f->declStmt->loc : f->arrow ? f->arrow->loc : f->body ? f->body->loc : Loc();
  line(start);
  prologue(f);
  if (f->isCtor && (!f->cls->parent)) genCtorFieldInits();
  if (f->exprBody) {
    Kind rk = kindOf(f->type->ret);
    if (rk == K_V) {
      genDiscard(f->exprBody);
      emit(OP_RET_V);
    } else {
      uint16_t r = gen(f->exprBody);
      static const uint16_t rets[3] = {OP_RET_I, OP_RET_F, OP_RET_R};
      emit(rets[rk], r);
    }
    return;
  }
  if (f->body) {
    blockEntry(f->body->stmts);
    bool first = true;
    for (Stmt* s : f->body->stmts) {
      genStmt(s);
      if (first && f->isCtor && f->cls->parent) genCtorFieldInits();
      first = false;
    }
  }
  if (f->isCtor && f->cls->parent && !emittedFieldInits) genCtorFieldInits();
  Kind rk = kindOf(f->type->ret);
  if (rk == K_V) emit(OP_RET_V);
  else if (f->type->ret->t == TT::Optional && f->type->ret->hasUndef) {
    uint16_t r = temp(K_R);
    emit(OP_LOADUNDEF, r);
    emit(OP_RET_R, r);
  } else {
    uint16_t r = temp(K_R);
    loadStr(r, u"function '" + utf8ToUtf16(f->name) + u"' ended without returning a value");
    emit(OP_ABORT, r);
  }
}

// ======================================================================= statements
void Gen::declareLocal(VarSym* v) {
  if (varReg.count(v)) return;
  if (v->captured) {
    uint16_t cell = allocReg(K_R);
    emit(OP_NEWCELL, cell, vtOf(v->type ? v->type : T.unknown()));
    varReg[v] = cell;
  } else {
    varReg[v] = allocReg(K(v->type ? v->type : T.unknown()));
  }
}

void Gen::blockEntry(std::vector<Stmt*>& stmts) {
  // cells for captured variables declared in this block (closures may be created before the declaration)
  std::function<void(VarDecl&)> pre = [&](VarDecl& d) {
    if (d.sym && !d.sym->isGlobal && d.sym->captured) declareLocal(d.sym);
    for (auto& sub : d.elems) pre(sub);
  };
  for (Stmt* s : stmts) {
    if (s->k == SK::Var)
      for (auto& d : s->decls) pre(d);
  }
  // hoisted nested functions
  std::vector<FuncSym*> nested;
  for (Stmt* s : stmts)
    if (s->k == SK::Func && s->fn && s->fn->selfVar) nested.push_back(s->fn);
  for (FuncSym* f : nested) declareLocal(f->selfVar);
  for (FuncSym* f : nested) {
    size_t m0 = mark();
    uint16_t r = temp(K_R);
    genClosure(f, r);
    writeVar(f->selfVar, r);
    release(m0);
  }
}

void Gen::genBlock(std::vector<Stmt*>& stmts) {
  // register reuse: free registers of variables declared in this block afterwards
  std::vector<VarSym*> declared;
  std::function<void(VarDecl&)> col = [&](VarDecl& d) {
    if (d.sym && !d.sym->isGlobal) declared.push_back(d.sym);
    for (auto& sub : d.elems) col(sub);
  };
  for (Stmt* s : stmts) {
    if (s->k == SK::Var) for (auto& d : s->decls) col(d);
    if (s->k == SK::Func && s->fn && s->fn->selfVar) declared.push_back(s->fn->selfVar);
  }
  blockEntry(stmts);
  for (Stmt* s : stmts) genStmt(s);
  for (VarSym* v : declared) {
    auto it = varReg.find(v);
    if (it != varReg.end()) {
      freeReg(it->second);
      varReg.erase(it);
    }
  }
}

void Gen::bindVar(VarSym* v, uint16_t valueReg) {
  if (v->isGlobal) {
    static const uint16_t ops[3] = {OP_GSET_I, OP_GSET_F, OP_GSET_R};
    emitBx(ops[K(v->type)], valueReg, (uint32_t)v->globalIdx);
    return;
  }
  declareLocal(v);
  writeVar(v, valueReg);
}

void Gen::genVarDecl(VarDecl& d, bool topLevel) {
  (void)topLevel;
  if (!d.isArrayPat && !d.isObjPat) {
    VarSym* v = d.sym;
    if (!v) return;
    if (v->isGlobal) {
      static const uint16_t ops[3] = {OP_GSET_I, OP_GSET_F, OP_GSET_R};
      size_t m0 = mark();
      uint16_t r;
      if (d.init) r = gen(d.init);
      else {
        r = temp(K(v->type));
        if (v->type->t == TT::Optional && v->type->hasUndef) emit(OP_LOADUNDEF, r);
        else loadZero(r, K(v->type));
      }
      emitBx(ops[K(v->type)], r, (uint32_t)v->globalIdx);
      release(m0);
      return;
    }
    bool existed = varReg.count(v) > 0;
    declareLocal(v);
    if (!v->captured) {
      uint16_t r = varReg[v];
      if (d.init) genInto(d.init, r);
      else if (v->type->t == TT::Optional && v->type->hasUndef) emit(OP_LOADUNDEF, r);
      else loadZero(r, K(v->type));
    } else {
      size_t m0 = mark();
      uint16_t r;
      if (d.init) r = gen(d.init);
      else {
        r = temp(K(v->type));
        if (v->type->t == TT::Optional && v->type->hasUndef) emit(OP_LOADUNDEF, r);
        else loadZero(r, K(v->type));
      }
      if (existed) {
        // re-entering a loop body: a fresh binding per iteration
        uint16_t cell = varReg[v];
        emit(OP_NEWCELL, cell, vtOf(v->type));
      }
      writeVar(v, r);
      release(m0);
    }
    return;
  }
  size_t m0 = mark();
  uint16_t src = gen(d.init);
  destructure(d, src, d.init->type);
  release(m0);
}

void Gen::destructure(VarDecl& d, uint16_t src, Type* t) {
  if (!d.isArrayPat && !d.isObjPat) {
    if (!d.sym) return;
    if (!d.sym->isGlobal && d.sym->captured && varReg.count(d.sym)) emit(OP_NEWCELL, varReg[d.sym], vtOf(d.sym->type));
    bindVar(d.sym, src);
    return;
  }
  for (size_t i = 0; i < d.elems.size(); i++) {
    VarDecl& sub = d.elems[i];
    if (!sub.isArrayPat && !sub.isObjPat && !sub.sym) continue;
    size_t m0 = mark();
    Type* et;
    uint16_t r;
    if (d.isArrayPat) {
      if (t->t == TT::Tuple) {
        et = t->elems[i];
        r = temp(K(et));
        ClassSym* rc = C.tupleRecord(t);
        static const uint16_t ops[3] = {OP_GETF_I, OP_GETF_F, OP_GETF_R};
        emit(ops[K(et)], r, src, (uint16_t)rc->fieldIdx[std::to_string(i)]);
      } else {
        et = t->elem;
        r = temp(K(et));
        uint16_t ir = temp(K_I);
        loadInt(ir, (int64_t)i);
        emit(op3(K(et), OP_AGETI_I, OP_AGETI_F, OP_AGETI_R), r, src, ir);
      }
    } else {
      const std::string& key = d.keys[i];
      if (t->t == TT::Class) {
        int fi = t->cls->fieldIdx[key];
        et = t->cls->fields[fi].type;
        r = temp(K(et));
        emit(op3(K(et), OP_GETF_I, OP_GETF_F, OP_GETF_R), r, src, (uint16_t)fi);
      } else {
        et = t->shape->findProp(key)->type;
        r = temp(K(et));
        D->icNames.push_back(kStr(utf8ToUtf16(key)));
        emit(op3(K(et), OP_GETN_I, OP_GETN_F, OP_GETN_R), r, src, (uint16_t)(D->icNames.size() - 1));
      }
    }
    destructure(sub, r, et);
    release(m0);
  }
}

LoopCtx* Gen::findLoop(const std::string& label, bool forContinue) {
  for (auto it = loops.rbegin(); it != loops.rend(); ++it) {
    if (!label.empty()) {
      if (it->label == label) return &*it;
    } else if (forContinue ? it->isLoop : true) {
      if (forContinue || it->isLoop || it->cont == nullptr) return &*it;
    }
  }
  return nullptr;
}

void Gen::genStmt(Stmt* s) {
  line(s->loc);
  bool topLevel = fn->isInit && false;
  (void)topLevel;
  switch (s->k) {
    case SK::Var:
      for (auto& d : s->decls) genVarDecl(d, fn->isInit);
      break;
    case SK::Expr: {
      size_t m0 = mark();
      genDiscard(s->expr);
      release(m0);
      break;
    }
    case SK::Block:
      genBlock(s->stmts);
      break;
    case SK::If: {
      Label* elseL = newLabel();
      Label* end = newLabel();
      size_t m0 = mark();
      condJump(s->expr, false, elseL);
      release(m0);
      genStmt(s->then);
      if (s->els) jump(OP_JMP, 0, end);
      place(elseL);
      if (s->els) genStmt(s->els);
      place(end);
      break;
    }
    case SK::While: {
      Label* top = newLabel();
      Label* cond = newLabel();
      Label* end = newLabel();
      jump(OP_JMP, 0, cond);
      place(top);
      loops.push_back({pendingLabel, end, cond, true});
      pendingLabel.clear();
      genStmt(s->body);
      loops.pop_back();
      place(cond);
      size_t m0 = mark();
      condJump(s->expr, true, top);
      release(m0);
      place(end);
      break;
    }
    case SK::DoWhile: {
      Label* top = newLabel();
      Label* cont = newLabel();
      Label* end = newLabel();
      place(top);
      loops.push_back({pendingLabel, end, cont, true});
      pendingLabel.clear();
      genStmt(s->body);
      loops.pop_back();
      place(cont);
      size_t m0 = mark();
      condJump(s->expr, true, top);
      release(m0);
      place(end);
      break;
    }
    case SK::For: {
      std::string label = pendingLabel;
      pendingLabel.clear();
      std::vector<VarSym*> loopVars;
      if (s->init) {
        if (s->init->k == SK::Var) {
          for (auto& d : s->init->decls) {
            if (d.sym && d.sym->captured) declareLocal(d.sym);
            genVarDecl(d, false);
            if (d.sym) loopVars.push_back(d.sym);
          }
        } else {
          genStmt(s->init);
        }
      }
      Label* top = newLabel();
      Label* cont = newLabel();
      Label* cond = newLabel();
      Label* end = newLabel();
      if (s->expr) jump(OP_JMP, 0, cond);
      place(top);
      loops.push_back({label, end, cont, true});
      genStmt(s->body);
      loops.pop_back();
      place(cont);
      // per-iteration bindings for captured loop variables
      for (VarSym* v : loopVars) {
        if (!v->captured) continue;
        size_t m0 = mark();
        uint16_t val = temp(K(v->type));
        readVar(v, val, v->type);
        emit(OP_NEWCELL, varReg[v], vtOf(v->type));
        writeVar(v, val);
        release(m0);
      }
      if (s->update) {
        size_t m0 = mark();
        genDiscard(s->update);
        release(m0);
      }
      if (s->expr) {
        place(cond);
        size_t m0 = mark();
        condJump(s->expr, true, top);
        release(m0);
      } else {
        jump(OP_JMP, 0, top);
      }
      place(end);
      for (VarSym* v : loopVars) {
        auto it = varReg.find(v);
        if (it != varReg.end()) { freeReg(it->second); varReg.erase(it); }
      }
      break;
    }
    case SK::ForOf:
      genForOf(s);
      break;
    case SK::Return: {
      size_t m0 = mark();
      if (fn->isCtor) { emit(OP_RET_V); break; }
      Kind rk = kindOf(fn->type->ret);
      if (!s->expr) {
        if (rk == K_V) emit(OP_RET_V);
        else {
          uint16_t r = temp(K_R);
          emit(OP_LOADUNDEF, r);
          emit(OP_RET_R, r);
        }
      } else if (rk == K_V) {
        genDiscard(s->expr);
        emit(OP_RET_V);
      } else {
        uint16_t r = gen(s->expr);
        static const uint16_t rets[3] = {OP_RET_I, OP_RET_F, OP_RET_R};
        emit(rets[rk], r);
      }
      release(m0);
      break;
    }
    case SK::Break: {
      LoopCtx* lc = nullptr;
      for (auto it = loops.rbegin(); it != loops.rend(); ++it) {
        if (!s->label.empty()) {
          if (it->label == s->label) { lc = &*it; break; }
        } else if (it->isLoop || it->isSwitch) {
          lc = &*it;
          break;
        }
      }
      if (!lc) { err(s->loc, "invalid break"); break; }
      jump(OP_JMP, 0, lc->brk);
      break;
    }
    case SK::Continue: {
      LoopCtx* lc = nullptr;
      for (auto it = loops.rbegin(); it != loops.rend(); ++it) {
        if (!it->isLoop) continue;
        if (!s->label.empty() && it->label != s->label) continue;
        lc = &*it;
        break;
      }
      if (!lc) { err(s->loc, "invalid continue"); break; }
      jump(OP_JMP, 0, lc->cont);
      break;
    }
    case SK::Labeled: {
      Stmt* b = s->body;
      bool isLoop = b->k == SK::While || b->k == SK::DoWhile || b->k == SK::For || b->k == SK::ForOf;
      if (isLoop) {
        pendingLabel = s->label;
        genStmt(b);
        pendingLabel.clear();
      } else {
        Label* end = newLabel();
        loops.push_back({s->label, end, nullptr, false});
        genStmt(b);
        loops.pop_back();
        place(end);
      }
      break;
    }
    case SK::Switch:
      genSwitch(s);
      break;
    case SK::Throw: {
      size_t m0 = mark();
      Expr* e = s->expr;
      uint16_t msg;
      if (e->k == EK::New) msg = gen(e->list[0]);
      else msg = gen(e);
      emit(OP_ABORT, msg);
      release(m0);
      break;
    }
    case SK::Class: {
      if (!s->cls) break;
      for (auto& m : s->members) {
        if (m.kind != ClassMember::Field || !m.isStatic) continue;
        auto it = s->cls->staticFields.find(m.name);
        if (it == s->cls->staticFields.end()) continue;
        VarSym* v = it->second;
        size_t m0 = mark();
        uint16_t r;
        if (m.init) r = gen(m.init);
        else {
          r = temp(K(v->type));
          if (v->type->t == TT::Optional && v->type->hasUndef) emit(OP_LOADUNDEF, r);
          else loadZero(r, K(v->type));
        }
        static const uint16_t ops[3] = {OP_GSET_I, OP_GSET_F, OP_GSET_R};
        emitBx(ops[K(v->type)], r, (uint32_t)v->globalIdx);
        release(m0);
      }
      break;
    }
    default:
      break;
  }
}


// ======================================================================= loops over collections
void Gen::genForOf(Stmt* s) {
  std::string label = pendingLabel;
  pendingLabel.clear();
  size_t m0 = mark();
  Type* t = s->expr->type;
  uint16_t coll = temp(K_R);
  genInto(s->expr, coll);
  VarDecl& d = s->decls[0];
  int mode = s->declareKind;
  Label* top = newLabel();
  Label* cont = newLabel();
  Label* end = newLabel();
  uint16_t i = temp(K_I);
  uint16_t aux = temp(K_I);
  loadInt(i, 0);
  // collect loop variables for cleanup
  std::vector<VarSym*> vars;
  std::function<void(VarDecl&)> col = [&](VarDecl& x) {
    if (x.sym) vars.push_back(x.sym);
    for (auto& sub : x.elems) col(sub);
  };
  col(d);
  for (VarSym* v : vars) declareLocal(v);
  auto bindElem = [&](uint16_t elem, Type* et) {
    size_t m1 = mark();
    destructure(d, elem, et);
    release(m1);
  };
  place(top);
  size_t mLoop = mark();
  auto pairBind = [&](uint16_t a, Type* ta, uint16_t b, Type* tb, Type* tupleT) {
    if (d.isArrayPat && d.elems.size() <= 2) {
      if (d.elems.size() > 0) { size_t m1 = mark(); destructure(d.elems[0], a, ta); release(m1); }
      if (d.elems.size() > 1) { size_t m1 = mark(); destructure(d.elems[1], b, tb); release(m1); }
    } else {
      ClassSym* rc = C.tupleRecord(tupleT);
      uint16_t tup = temp(K_R);
      emit(OP_NEW, tup, (uint16_t)rc->classIdx);
      emit(op3(K(ta), OP_SETF_I, OP_SETF_F, OP_SETF_R), tup, 0, a);
      emit(op3(K(tb), OP_SETF_I, OP_SETF_F, OP_SETF_R), tup, 1, b);
      bindElem(tup, tupleT);
    }
  };
  if (t->t == TT::Array && mode <= 3) {
    uint16_t n = temp(K_I), c = temp(K_I);
    emit(OP_ALENI, n, coll);
    emit(OP_LT_I, c, i, n);
    jump(OP_JF, c, end);
    Type* et = t->elem;
    if (mode == 2) {
      uint16_t fi = temp(K_F);
      emit(OP_I2F, fi, i);
      bindElem(fi, T.number());
    } else {
      uint16_t v = temp(K(et));
      emit(op3(K(et), OP_AGETI_I, OP_AGETI_F, OP_AGETI_R), v, coll, i);
      if (mode == 1) {
        uint16_t fi = temp(K_F);
        emit(OP_I2F, fi, i);
        pairBind(fi, T.number(), v, et, T.tuple({T.number(), et}));
      } else {
        bindElem(v, et);
      }
    }
  } else if (t->t == TT::Bytes) {
    uint16_t n = temp(K_I), c = temp(K_I);
    emit(OP_BLENI, n, coll);
    emit(OP_LT_I, c, i, n);
    jump(OP_JF, c, end);
    uint16_t v = temp(K_I), fv = temp(K_F);
    emit(OP_BGETI, v, coll, i);
    emit(OP_I2F, fv, v);
    bindElem(fv, T.number());
  } else if (t->t == TT::String) {
    uint16_t c = temp(K_I), zero = temp(K_I);
    callNative("String.iterNext", {coll, i}, aux, 'I');
    loadInt(zero, 0);
    emit(OP_LT_I, c, aux, zero);
    jump(OP_JT, c, end);
    uint16_t ch = temp(K_R);
    callNative("String.codePointStr", {coll, i}, ch, 'R');
    bindElem(ch, T.string());
  } else if (t->t == TT::Map || t->t == TT::Set) {
    uint16_t c = temp(K_I), zero = temp(K_I);
    callNative("Map.next", {coll, i}, i, 'I');
    loadInt(zero, 0);
    emit(OP_LT_I, c, i, zero);
    jump(OP_JT, c, end);
    Type* kt = t->t == TT::Map ? t->key : t->elem;
    Type* vt = t->t == TT::Map ? t->val : t->elem;
    uint16_t k = temp(K(kt));
    callNative("Map.keyAt" + kindSuffixOf(kt), {coll, i}, k, kindChar(K(kt)));
    if (t->t == TT::Set) {
      if (mode == 8) pairBind(k, kt, k, kt, T.tuple({kt, kt}));
      else bindElem(k, kt);
    } else if (mode == 4) {
      bindElem(k, kt);
    } else {
      uint16_t v = temp(K(vt));
      callNative("Map.valAt" + kindSuffixOf(vt), {coll, i}, v, kindChar(K(vt)));
      if (mode == 5) bindElem(v, vt);
      else pairBind(k, kt, v, vt, T.tuple({kt, vt}));
    }
  } else {
    err(s->loc, "cannot iterate over '" + typeName(t) + "'");
  }
  release(mLoop);
  loops.push_back({label, end, cont, true});
  genStmt(s->body);
  loops.pop_back();
  place(cont);
  if (t->t == TT::String) emit(OP_MOV_I, i, aux);
  else emit(OP_ADDI_I, i, i, 1);
  jump(OP_JMP, 0, top);
  place(end);
  for (VarSym* v : vars) {
    auto it = varReg.find(v);
    if (it != varReg.end()) { freeReg(it->second); varReg.erase(it); }
  }
  release(m0);
}

void Gen::genSwitch(Stmt* s) {
  std::string label = pendingLabel;
  pendingLabel.clear();
  size_t m0 = mark();
  Type* dt = s->expr->type;
  uint16_t disc = temp(K(dt));
  genInto(s->expr, disc);
  Label* end = newLabel();
  std::vector<Label*> bodies;
  Label* def = nullptr;
  for (auto& c : s->cases) {
    Label* l = newLabel();
    bodies.push_back(l);
    if (!c.test) { def = l; continue; }
    size_t m1 = mark();
    uint16_t v = gen(c.test);
    uint16_t cmp = temp(K_I);
    Kind k = K(dt);
    if (dt->t == TT::String || (dt->t == TT::Enum && dt->en->isString)) emit(OP_SEQ, cmp, disc, v);
    else if (k == K_F) emit(OP_EQ_F, cmp, disc, v);
    else if (k == K_I) emit(OP_EQ_I, cmp, disc, v);
    else emit(OP_EQ_R, cmp, disc, v);
    jump(OP_JT, cmp, l);
    release(m1);
  }
  jump(OP_JMP, 0, def ? def : end);
  std::vector<Stmt*> all;
  for (auto& c : s->cases)
    for (Stmt* x : c.body) all.push_back(x);
  LoopCtx lc{label, end, nullptr, false};
  lc.isSwitch = true;
  loops.push_back(lc);
  blockEntry(all);
  for (size_t i = 0; i < s->cases.size(); i++) {
    place(bodies[i]);
    for (Stmt* x : s->cases[i].body) genStmt(x);
  }
  loops.pop_back();
  place(end);
  release(m0);
}

// ======================================================================= variables
void Gen::readVar(VarSym* v, uint16_t dst, Type* want) {
  Type* st = v->type;
  if (!want) want = st;
  static const uint16_t gget[3] = {OP_GGET_I, OP_GGET_F, OP_GGET_R};
  static const uint16_t cget[3] = {OP_CGET_I, OP_CGET_F, OP_CGET_R};
  static const uint16_t uget[3] = {OP_UGET_I, OP_UGET_F, OP_UGET_R};
  bool needUnbox = isBoxedOptional(st) && want->t != TT::Optional && kindOf(want) != K_R;
  uint16_t tgt = needUnbox ? temp(K_R) : dst;
  if (v->isGlobal) {
    emitBx(gget[K(st)], tgt, (uint32_t)v->globalIdx);
  } else {
    auto it = varReg.find(v);
    if (it != varReg.end()) {
      if (v->captured) emit(cget[K(st)], tgt, it->second);
      else mov(tgt, it->second, K(st));
    } else {
      auto ci = fn->captureIndex.find(v);
      if (ci == fn->captureIndex.end()) {
        err(Loc(), "internal error: variable '" + v->name + "' is not accessible in '" + fn->name + "'");
        return;
      }
      emit(uget[K(st)], tgt, (uint16_t)ci->second);
    }
  }
  if (needUnbox) emit(kindOf(want) == K_F ? OP_UNBOX_F : OP_UNBOX_I, dst, tgt);
}

void Gen::writeVar(VarSym* v, uint16_t src) {
  Type* st = v->type;
  static const uint16_t gset[3] = {OP_GSET_I, OP_GSET_F, OP_GSET_R};
  static const uint16_t cset[3] = {OP_CSET_I, OP_CSET_F, OP_CSET_R};
  static const uint16_t uset[3] = {OP_USET_I, OP_USET_F, OP_USET_R};
  if (v->isGlobal) {
    emitBx(gset[K(st)], src, (uint32_t)v->globalIdx);
    return;
  }
  auto it = varReg.find(v);
  if (it != varReg.end()) {
    if (v->captured) emit(cset[K(st)], it->second, src);
    else mov(it->second, src, K(st));
    return;
  }
  auto ci = fn->captureIndex.find(v);
  if (ci == fn->captureIndex.end()) {
    err(Loc(), "internal error: variable '" + v->name + "' is not writable in '" + fn->name + "'");
    return;
  }
  emit(uset[K(st)], (uint16_t)ci->second, src);
}

void Gen::genClosure(FuncSym* f, uint16_t dst) {
  std::vector<uint16_t> specs;
  for (VarSym* v : f->captures) {
    auto it = varReg.find(v);
    if (it != varReg.end() && v->captured) {
      specs.push_back(it->second);
      continue;
    }
    auto ci = fn->captureIndex.find(v);
    if (ci != fn->captureIndex.end()) {
      specs.push_back((uint16_t)(0x8000 | ci->second));
      continue;
    }
    err(Loc(), "internal error: cannot capture '" + v->name + "' for '" + f->name + "'");
    specs.push_back(0);
  }
  emit(OP_CLOSURE, dst, (uint16_t)f->funcIdx, (uint16_t)specs.size());
  emitRegList(specs);
}

// ======================================================================= expressions
uint16_t Gen::gen(Expr* e) {
  Expr* x = strip(e);
  if (x->k == EK::Num && x->type && x->type->t == TT::Number) return numConst(x->num);
  if (x->k == EK::Ident && x->intrinsic == IN_CONST_NUM && x->type && x->type->t == TT::Number) return numConst(x->num);
  if (x->k == EK::Ident && x->var && !x->var->isGlobal && x->intrinsic == 0 && !x->var->captured) {
    auto it = varReg.find(x->var);
    if (it != varReg.end() && (!isBoxedOptional(x->var->type) || x->type == x->var->type)) return it->second;
  }
  if (x->k == EK::This && x->var && !x->var->captured) {
    auto it = varReg.find(x->var);
    if (it != varReg.end()) return it->second;
  }
  if (!e->type || kindOf(e->type) == K_V) {
    genInto(e, kNoReg);
    return kNoReg;
  }
  uint16_t r = temp(K(e->type));
  genInto(e, r);
  return r;
}

void Gen::genDiscard(Expr* e) {
  Expr* x = strip(e);
  if (x->k == EK::Assign) { genAssign(x, kNoReg); return; }
  if (x->k == EK::Update) {
    // statement-level ++/--
    genInto(x, kNoReg);
    return;
  }
  if (x->k == EK::Call || x->k == EK::New) {
    if (!x->type || kindOf(x->type) == K_V) { genInto(x, kNoReg); return; }
  }
  size_t m0 = mark();
  gen(e);
  release(m0);
}

void Gen::truthyOf(uint16_t dst, uint16_t src, Type* t) {
  switch (t->t) {
    case TT::Bool: mov(dst, src, K_I); break;
    case TT::Number: emit(OP_F2B, dst, src); break;
    case TT::Int: {
      uint16_t tmp = temp(K_I);
      emit(OP_NOT_B, tmp, src);
      emit(OP_NOT_B, dst, tmp);
      break;
    }
    case TT::String: emit(OP_STRTRUTHY, dst, src); break;
    case TT::Enum:
      if (t->en->isString) emit(OP_STRTRUTHY, dst, src);
      else emit(OP_F2B, dst, src);
      break;
    case TT::Null: case TT::Undefined: loadInt(dst, 0); break;
    case TT::Optional: {
      Type* b = t->elem;
      if (b->t == TT::String || b->t == TT::BigInt || kindOf(b) != K_R || (b->t == TT::Enum && b->en->isString)) {
        callNative("Value.truthy", {src}, dst, 'I');
      } else {
        uint16_t tmp = temp(K_I);
        emit(OP_ISNULLISH, tmp, src);
        emit(OP_NOT_B, dst, tmp);
      }
      break;
    }
    case TT::BigInt: callNative("Value.truthy", {src}, dst, 'I'); break;
    default: loadInt(dst, 1); break;
  }
}

uint16_t Gen::genTruthy(Expr* e) {
  Expr* x = strip(e);
  if (x->k == EK::Logical && (x->raw == "&&" || x->raw == "||") && x->intrinsic != 2) {
    uint16_t dst = temp(K_I);
    Label* end = newLabel();
    uint16_t a = genTruthy(x->a);
    mov(dst, a, K_I);
    jump(x->raw == "&&" ? OP_JF : OP_JT, dst, end);
    uint16_t b = genTruthy(x->b);
    mov(dst, b, K_I);
    place(end);
    return dst;
  }
  uint16_t r = gen(e);
  if (e->type->t == TT::Bool) return r;
  uint16_t dst = temp(K_I);
  truthyOf(dst, r, e->type);
  return dst;
}

void Gen::condJump(Expr* e, bool jumpIf, Label* target) {
  Expr* x = strip(e);
  if (x->k == EK::Unary && x->raw == "!") { condJump(x->a, !jumpIf, target); return; }
  if (x->k == EK::Logical && x->intrinsic != 2 && (x->raw == "&&" || x->raw == "||")) {
    bool isAnd = x->raw == "&&";
    if (isAnd != jumpIf) {
      // (a && b) false -> a false or b false ; (a || b) true -> a true or b true
      condJump(x->a, jumpIf, target);
      condJump(x->b, jumpIf, target);
    } else {
      Label* skip = newLabel();
      condJump(x->a, !jumpIf, skip);
      condJump(x->b, jumpIf, target);
      place(skip);
    }
    return;
  }
  if (x->k == EK::Bool) {
    if (x->bval == jumpIf) jump(OP_JMP, 0, target);
    return;
  }
  if (allowFused && x->k == EK::Binary && x->a->type && x->b->type) {
    const std::string& op = x->raw;
    Type* lt = x->a->type;
    Type* rt = x->b->type;
    bool isF = (lt->t == TT::Number || (lt->t == TT::Enum && !lt->en->isString)) &&
               (rt->t == TT::Number || (rt->t == TT::Enum && !rt->en->isString));
    bool isI = lt->t == TT::Int && lt == rt && (isSignedInt(lt) || intBits(lt) < 64);
    bool isB = lt->t == TT::Bool && rt->t == TT::Bool;
    if ((isF || isI || (isB && (op == "===" || op == "!=="))) &&
        (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "===" || op == "!==")) {
      size_t m0 = mark();
      uint16_t a = gen(x->a);
      uint16_t b = gen(x->b);
      bool fl = isF;
      uint16_t opc = 0;
      bool swap = op == ">" || op == ">=";
      if (op == "<" || op == ">") opc = jumpIf ? (fl ? OP_JLT_F : OP_JLT_I) : (fl ? OP_JNLT_F : OP_JNLT_I);
      else if (op == "<=" || op == ">=") opc = jumpIf ? (fl ? OP_JLE_F : OP_JLE_I) : (fl ? OP_JNLE_F : OP_JNLE_I);
      else if (op == "===") opc = jumpIf ? (fl ? OP_JEQ_F : OP_JEQ_I) : (fl ? OP_JNE_F : OP_JNE_I);
      else opc = jumpIf ? (fl ? OP_JNE_F : OP_JNE_I) : (fl ? OP_JEQ_F : OP_JEQ_I);
      if (swap) jumpRel(opc, b, a, target);
      else jumpRel(opc, a, b, target);
      release(m0);
      return;
    }
  }
  size_t m0 = mark();
  uint16_t r = genTruthy(x);
  jump(jumpIf ? OP_JT : OP_JF, r, target);
  release(m0);
}

uint16_t Gen::toStringReg(uint16_t src, Type* t) {
  switch (t->t) {
    case TT::String: return src;
    case TT::Number: { uint16_t r = temp(K_R); emit(OP_NUM2STR, r, src); return r; }
    case TT::Int: { uint16_t r = temp(K_R); emit(OP_INT2STR, r, src, t->vt); return r; }
    case TT::Bool: { uint16_t r = temp(K_R); emit(OP_BOOL2STR, r, src); return r; }
    case TT::Enum: {
      if (t->en->isString) return src;
      uint16_t r = temp(K_R);
      emit(OP_NUM2STR, r, src);
      return r;
    }
    case TT::BigInt: {
      uint16_t r = temp(K_R), radix = temp(K_F);
      loadNum(radix, 10);
      callNative("BigInt.toString", {src, radix}, r, 'R');
      return r;
    }
    default: {
      uint16_t r = temp(K_R);
      if (kindOf(t) != K_R) {
        uint16_t b = temp(K_R);
        boxIfNeeded(b, src, t, T.optional(t, false, true));
        callNative("Value.toString", {b}, r, 'R');
      } else {
        callNative("Value.toString", {src}, r, 'R');
      }
      return r;
    }
  }
}

void Gen::genInto(Expr* e, uint16_t dst) {
  line(e->loc);
  switch (e->k) {
    case EK::Num:
      if (dst == kNoReg) return;
      if (e->type->t == TT::Int) loadInt(dst, e->ival);
      else loadNum(dst, e->num);
      return;
    case EK::BigLit: {
      if (dst == kNoReg) return;
      uint16_t s = temp(K_R);
      loadStr(s, utf8ToUtf16(e->raw));
      callNative("BigInt.parseLiteral", {s}, dst, 'R');
      return;
    }
    case EK::Str: if (dst != kNoReg) loadStr(dst, e->str); return;
    case EK::Bool: if (dst != kNoReg) loadInt(dst, e->bval ? 1 : 0); return;
    case EK::Null: if (dst != kNoReg) emit(OP_LOADNULL, dst); return;
    case EK::Undefined: if (dst != kNoReg) emit(OP_LOADUNDEF, dst); return;
    case EK::TypeOf: if (dst != kNoReg) loadStr(dst, e->str); return;
    case EK::Template: {
      if (dst == kNoReg) { for (Expr* x : e->list) genDiscard(x); return; }
      size_t m0 = mark();
      uint16_t acc = temp(K_R);
      loadStr(acc, e->quasis[0]);
      for (size_t i = 0; i < e->list.size(); i++) {
        uint16_t v = gen(e->list[i]);
        uint16_t s = toStringReg(v, e->list[i]->type);
        emit(OP_SCAT, acc, acc, s);
        if (i + 1 < e->quasis.size() && !e->quasis[i + 1].empty()) {
          uint16_t q = temp(K_R);
          loadStr(q, e->quasis[i + 1]);
          emit(OP_SCAT, acc, acc, q);
        }
      }
      mov(dst, acc, K_R);
      release(m0);
      return;
    }
    case EK::Ident: {
      if (dst == kNoReg) return;
      if (e->intrinsic == IN_CONST_NUM) { loadNum(dst, e->num); return; }
      if (e->var) { readVar(e->var, dst, e->type); return; }
      if (e->callee) {
        if (e->callee->selfVar) readVar(e->callee->selfVar, dst, e->type);
        else genClosure(e->callee, dst);
        return;
      }
      err(e->loc, "internal error: unresolved identifier");
      return;
    }
    case EK::This:
      if (dst != kNoReg) readVar(e->var, dst, e->type);
      return;
    case EK::Array: {
      size_t m0 = mark();
      uint16_t d = dst == kNoReg ? temp(K_R) : dst;
      genArrayLit(e, d);
      release(m0);
      return;
    }
    case EK::Object: {
      size_t m0 = mark();
      uint16_t d = dst == kNoReg ? temp(K_R) : dst;
      genObjectLit(e, d);
      release(m0);
      return;
    }
    case EK::Unary: {
      size_t m0 = mark();
      if (e->intrinsic == IN_CONST_NUM) {
        if (dst != kNoReg) loadInt(dst, e->ival);
        return;
      }
      const std::string& op = e->raw;
      uint16_t d = dst == kNoReg ? temp(K(e->type)) : dst;
      if (op == "!") {
        uint16_t t = genTruthy(e->a);
        emit(OP_NOT_B, d, t);
      } else if (op == "-") {
        Type* t = e->a->type;
        uint16_t a = gen(e->a);
        if (t->t == TT::Int) {
          emit(OP_NEG_I, d, a);
          wrapInt(d, d, t);
        } else if (t->t == TT::BigInt) {
          callNative("BigInt.neg", {a}, d, 'R');
        } else {
          emit(OP_NEG_F, d, a);
        }
      } else if (op == "+") {
        Type* t = e->a->type;
        uint16_t a = gen(e->a);
        if (t->t == TT::String) callNative("Global.Number", {a}, d, 'F');
        else if (t->t == TT::Bool) emit(OP_I2F, d, a);
        else mov(d, a, K_F);
      } else if (op == "~") {
        Type* t = e->a->type;
        uint16_t a = gen(e->a);
        if (t->t == TT::Int) {
          emit(OP_NOT_I, d, a);
          wrapInt(d, d, t);
        } else if (t->t == TT::BigInt) {
          callNative("BigInt.not", {a}, d, 'R');
        } else {
          emit(OP_BNOT_F, d, a);
        }
      }
      release(m0);
      return;
    }
    case EK::Update: {
      size_t m0 = mark();
      Expr* tg = strip(e->a);
      Type* t = e->a->type;
      if (tg->k == EK::Ident && tg->var && !tg->var->isGlobal && !tg->var->captured && varReg.count(tg->var)) {
        uint16_t r = varReg[tg->var];
        if (dst != kNoReg && !e->prefix) mov(dst, r, K(t));
        if (t->t == TT::Int) {
          emit(OP_ADDI_I, r, r, (uint16_t)(int16_t)(e->raw == "++" ? 1 : -1));
          wrapInt(r, r, t);
        } else if (t->t == TT::BigInt) {
          uint16_t one = temp(K_R), s1 = temp(K_R);
          loadStr(s1, u"1");
          callNative("BigInt.parseLiteral", {s1}, one, 'R');
          callNative(e->raw == "++" ? "BigInt.add" : "BigInt.sub", {r, one}, r, 'R');
        } else {
          uint16_t one = numConst(1);
          emit(e->raw == "++" ? OP_ADD_F : OP_SUB_F, r, r, one);
        }
        if (dst != kNoReg && e->prefix) mov(dst, r, K(t));
        release(m0);
        return;
      }
      // general lvalue: read, modify, write (object/index evaluated once)
      Expr* fake = P.arena.expr(EK::Assign, e->loc);
      fake->raw = e->raw == "++" ? "+=" : "-=";
      fake->a = e->a;
      Expr* one = P.arena.expr(t->t == TT::BigInt ? EK::BigLit : EK::Num, e->loc);
      one->num = 1;
      one->isIntLit = true;
      one->ival = 1;
      one->raw = "1";
      one->type = t;
      fake->b = one;
      fake->type = t;
      if (e->prefix || dst == kNoReg) {
        genAssign(fake, dst);
      } else {
        // postfix with value: old value
        uint16_t old = temp(K(t));
        genInto(e->a, old);
        genAssign(fake, kNoReg);
        mov(dst, old, K(t));
      }
      release(m0);
      return;
    }
    case EK::Binary: {
      size_t m0 = mark();
      uint16_t d = dst == kNoReg ? temp(K(e->type)) : dst;
      genBinary(e, d);
      release(m0);
      return;
    }
    case EK::Logical: {
      size_t m0 = mark();
      uint16_t d = dst == kNoReg ? temp(K(e->type)) : dst;
      const std::string& op = e->raw;
      if (op == "??") {
        Type* lt = e->a->type;
        Expr* la = strip(e->a);
        // m.get(k) ?? v  ->  Map.getOr
        if (la->k == EK::Call && la->native.rfind("Map.get", 0) == 0 && lt->t == TT::Optional && e->type == lt->elem) {
          Expr* mem = strip(la->a);
          uint16_t mreg = gen(mem->a);
          uint16_t k = gen(la->list[0]);
          uint16_t def = gen(e->b);
          Type* mt = baseOf(mem->a->type);
          callNative("Map.getOr" + kindSuffixOf(mt->key) + kindSuffixOf(mt->val), {mreg, k, def}, d, kindChar(K(e->type)));
          release(m0);
          return;
        }
        Label* useB = newLabel();
        Label* end = newLabel();
        uint16_t a = gen(e->a);
        if (lt->t != TT::Optional && !Checker::isNullish(lt)) { mov(d, a, K(e->type)); release(m0); return; }
        jump(OP_JNULLISH, a, useB);
        unboxNarrowed(d, a, lt, e->type);
        jump(OP_JMP, 0, end);
        place(useB);
        genInto(e->b, d);
        place(end);
        release(m0);
        return;
      }
      if (e->intrinsic == 2) {
        // value-producing a || b
        Label* useB = newLabel();
        Label* end = newLabel();
        uint16_t a = gen(e->a);
        uint16_t t = temp(K_I);
        truthyOf(t, a, e->a->type);
        jump(OP_JF, t, useB);
        unboxNarrowed(d, a, e->a->type, e->type);
        jump(OP_JMP, 0, end);
        place(useB);
        genInto(e->b, d);
        place(end);
        release(m0);
        return;
      }
      uint16_t t = genTruthy(e);
      mov(d, t, K_I);
      release(m0);
      return;
    }
    case EK::Assign:
      genAssign(e, dst);
      return;
    case EK::Cond: {
      size_t m0 = mark();
      Label* els = newLabel();
      Label* end = newLabel();
      condJump(e->a, false, els);
      if (dst == kNoReg) genDiscard(e->b); else genInto(e->b, dst);
      jump(OP_JMP, 0, end);
      place(els);
      if (dst == kNoReg) genDiscard(e->c); else genInto(e->c, dst);
      place(end);
      release(m0);
      return;
    }
    case EK::Call: {
      size_t m0 = mark();
      genCall(e, dst);
      release(m0);
      return;
    }
    case EK::New: {
      size_t m0 = mark();
      uint16_t d = dst == kNoReg ? temp(K_R) : dst;
      genNew(e, d);
      release(m0);
      return;
    }
    case EK::Member: {
      size_t m0 = mark();
      uint16_t d = dst == kNoReg ? temp(K(e->type)) : dst;
      genMember(e, d);
      release(m0);
      return;
    }
    case EK::Index: {
      size_t m0 = mark();
      uint16_t d = dst == kNoReg ? temp(K(e->type)) : dst;
      genIndex(e, d);
      release(m0);
      return;
    }
    case EK::NonNull: {
      size_t m0 = mark();
      uint16_t d = dst == kNoReg ? temp(K(e->type)) : dst;
      Expr* in = strip(e->a);
      // strict variants avoid boxing: arr.pop()!, m.get(k)!
      if (in->k == EK::Call && (in->native == "Array.pop" || in->native == "Array.shift" || in->native.rfind("Map.get", 0) == 0) &&
          in->a->k == EK::Member) {
        Expr* mem = strip(in->a);
        uint16_t recv = gen(mem->a);
        std::vector<uint16_t> args{recv};
        for (Expr* a : in->list) args.push_back(gen(a));
        std::string nm;
        if (in->native.rfind("Map.get", 0) == 0) {
          Type* mt = baseOf(mem->a->type);
          nm = "Map.getStrict" + kindSuffixOf(mt->key) + kindSuffixOf(mt->val);
        } else {
          nm = in->native + "Strict" + kindSuffixOf(e->type);
        }
        callNative(nm, args, d, kindChar(K(e->type)));
        release(m0);
        return;
      }
      uint16_t a = gen(e->a);
      Type* at = e->a->type;
      if (isBoxedOptional(at) && kindOf(e->type) != K_R) {
        emit(kindOf(e->type) == K_F ? OP_UNBOX_F : OP_UNBOX_I, d, a);
      } else {
        if (at->t == TT::Optional) emit(OP_CHKNN, a);
        mov(d, a, K(e->type));
      }
      release(m0);
      return;
    }
    case EK::As: {
      size_t m0 = mark();
      uint16_t d = dst == kNoReg ? temp(K(e->type)) : dst;
      if (e->intrinsic == IN_JSON_PARSE) {
        Expr* call = strip(e->a);
        uint16_t text = gen(call->list[0]);
        uint16_t desc = temp(K_R);
        loadStr(desc, utf8ToUtf16(jsonDescriptor(e->type)));
        uint16_t boxed = temp(K_R);
        callNative("JSON.parse", {text, desc}, boxed, 'R');
        if (kindOf(e->type) != K_R) emit(kindOf(e->type) == K_F ? OP_UNBOX_F : OP_UNBOX_I, d, boxed);
        else mov(d, boxed, K_R);
        release(m0);
        return;
      }
      uint16_t a = gen(e->a);
      if (e->intrinsic == 1) {
        Label* ok = newLabel();
        uint16_t c = temp(K_I);
        emit(OP_INSTOF, c, a, (uint16_t)e->cls->classIdx);
        jump(OP_JT, c, ok);
        uint16_t msg = temp(K_R);
        loadStr(msg, u"invalid cast to " + utf8ToUtf16(e->cls->name));
        emit(OP_ABORT, msg);
        place(ok);
        mov(d, a, K_R);
      } else if (e->intrinsic == 2) {
        Type* at = e->a->type;
        if (isBoxedOptional(at) && kindOf(e->type) != K_R) emit(kindOf(e->type) == K_F ? OP_UNBOX_F : OP_UNBOX_I, d, a);
        else { emit(OP_CHKNN, a); mov(d, a, K(e->type)); }
      } else {
        mov(d, a, K(e->type));
      }
      release(m0);
      return;
    }
    case EK::Arrow:
      if (dst != kNoReg) genClosure(e->fn, dst);
      return;
    case EK::Paren:
      genInto(e->a, dst);
      return;
    case EK::Conv: {
      if (e->conv == ConvKind::NumToBox || e->conv == ConvKind::IntToBox) {
        size_t m0 = mark();
        uint16_t a = gen(e->a);
        if (dst != kNoReg) {
          if (e->conv == ConvKind::NumToBox) emit(OP_BOX_F, dst, a);
          else emit(OP_BOX_I, dst, a, vtOf(e->a->type));
        }
        release(m0);
        return;
      }
      if (dst == kNoReg) { genDiscard(e->a); return; }
      if (e->a->type && K(e->a->type) != K(e->type) && kindOf(e->a->type) != K_V) {
        // Never / unknown -> anything
        size_t m0 = mark();
        genDiscard(e->a);
        loadZero(dst, K(e->type));
        release(m0);
        return;
      }
      genInto(e->a, dst);
      return;
    }
    case EK::Super:
    case EK::Spread:
      err(e->loc, "internal error: unexpected expression");
      return;
  }
}

// ======================================================================= assignment
void Gen::storeTo(Expr* target, uint16_t v) {
  Expr* t = strip(target);
  size_t m0 = mark();
  switch (t->k) {
    case EK::Ident:
      writeVar(t->var, v);
      break;
    case EK::Member: {
      if (t->intrinsic == IN_STATIC_FIELD) { writeVar(t->var, v); break; }
      if (t->intrinsic == IN_ARR_LENGTH_SET) {
        uint16_t obj = gen(t->a);
        callNative("Array.setLength", {obj, v}, kNoReg, 'V');
        break;
      }
      uint16_t obj = gen(t->a);
      Type* st = target->type;
      if (t->field >= 0 && t->cls) {
        emit(op3(K(st), OP_SETF_I, OP_SETF_F, OP_SETF_R), obj, (uint16_t)t->field, v);
      } else {
        D->icNames.push_back(kStr(utf8ToUtf16(t->raw)));
        emit(op3(K(st), OP_SETN_I, OP_SETN_F, OP_SETN_R), obj, (uint16_t)(D->icNames.size() - 1), v);
      }
      break;
    }
    case EK::Index: {
      Type* ot = baseOf(t->a->type);
      uint16_t obj = gen(t->a);
      if (ot->t == TT::Tuple) {
        emit(op3(K(target->type), OP_SETF_I, OP_SETF_F, OP_SETF_R), obj, (uint16_t)t->field, v);
        break;
      }
      uint16_t idx = gen(t->b);
      bool intIdx = t->b->type->t == TT::Int;
      if (ot->t == TT::Bytes) {
        if (intIdx) {
          uint16_t f = temp(K_F);
          intToNum(f, idx, t->b->type);
          idx = f;
        }
        emit(OP_BSET, obj, idx, v);
      } else {
        Kind k = K(ot->elem);
        if (intIdx) emit(op3(k, OP_ASETI_I, OP_ASETI_F, OP_ASETI_R), obj, idx, v);
        else emit(op3(k, OP_ASET_I, OP_ASET_F, OP_ASET_R), obj, idx, v);
      }
      break;
    }
    default:
      err(t->loc, "internal error: bad assignment target");
  }
  release(m0);
}

static bool isLeaf(Expr* e) {
  e = strip(e);
  return e->k == EK::Ident || e->k == EK::Num || e->k == EK::Str || e->k == EK::Bool;
}

void Gen::genAssign(Expr* e, uint16_t dst) {
  size_t m0 = mark();
  Expr* target = strip(e->a);
  const std::string& op = e->raw;
  if (target->k == EK::Array) {
    Expr* rhs = strip(e->b);
    std::vector<uint16_t> vals;
    if (e->intrinsic == 1) {
      for (size_t i = 0; i < rhs->list.size(); i++) {
        uint16_t r = temp(K(rhs->list[i]->type));
        genInto(rhs->list[i], r);
        vals.push_back(r);
      }
    } else {
      uint16_t src = gen(e->b);
      Type* rt = e->b->type;
      for (size_t i = 0; i < target->list.size(); i++) {
        Type* et = rt->t == TT::Tuple ? rt->elems[i] : rt->elem;
        uint16_t r = temp(K(et));
        if (rt->t == TT::Tuple) emit(op3(K(et), OP_GETF_I, OP_GETF_F, OP_GETF_R), r, src, (uint16_t)i);
        else {
          uint16_t ir = temp(K_I);
          loadInt(ir, (int64_t)i);
          emit(op3(K(et), OP_AGETI_I, OP_AGETI_F, OP_AGETI_R), r, src, ir);
        }
        vals.push_back(r);
      }
    }
    for (size_t i = 0; i < target->list.size(); i++) storeTo(target->list[i], vals[i]);
    release(m0);
    return;
  }
  Type* lt = e->a->type;
  if (op == "=") {
    // direct write into a plain local register when safe
    if (target->k == EK::Ident && target->var && !target->var->isGlobal && !target->var->captured &&
        varReg.count(target->var)) {
      Expr* b = strip(e->b);
      uint16_t r = varReg[target->var];
      bool direct = b->k == EK::Num || b->k == EK::Str || b->k == EK::Bool || b->k == EK::Null ||
                    ((b->k == EK::Binary) && isLeaf(b->a) && isLeaf(b->b) && b->intrinsic != IN_STR_CONCAT) ||
                    (b->k == EK::Unary && isLeaf(b->a));
      if (direct && kindOf(e->b->type) == K(lt) && e->b->type == lt) {
        genInto(e->b, r);
        if (dst != kNoReg) mov(dst, r, K(lt));
        release(m0);
        return;
      }
    }
    uint16_t v = gen(e->b);
    // preserve the value: gen() may return a variable register
    if (target->k == EK::Ident) {
      storeTo(e->a, v);
    } else {
      uint16_t vt = temp(K(lt));
      mov(vt, v, K(lt));
      storeTo(e->a, vt);
      v = vt;
    }
    if (dst != kNoReg) mov(dst, v, K(lt));
    release(m0);
    return;
  }
  if (op == "??=") {
    Label* end = newLabel();
    uint16_t cur = temp(K_R);
    genInto(e->a, cur);
    jump(OP_JNOTNULLISH, cur, end);
    uint16_t v = gen(e->b);
    storeTo(e->a, v);
    mov(cur, v, K_R);
    place(end);
    if (dst != kNoReg) mov(dst, cur, K_R);
    release(m0);
    return;
  }
  // compound assignment: evaluate the target location once
  std::string bop = op.substr(0, op.size() - 1);
  Kind k = K(lt);
  if (target->k == EK::Ident && target->var && !target->var->isGlobal && !target->var->captured &&
      varReg.count(target->var) && target->var->type == lt) {
    uint16_t vr = varReg[target->var];
    if (bop == "+" && (lt->t == TT::String || (lt->t == TT::Enum && lt->en->isString))) {
      uint16_t r = gen(e->b);
      uint16_t sr = toStringReg(r, e->b->type);
      emit(OP_SCAT, vr, vr, sr);
    } else {
      uint16_t r = gen(e->b);
      binaryOp(bop, lt, e->b->type, vr, vr, r, e->loc);
    }
    if (dst != kNoReg) mov(dst, vr, k);
    release(m0);
    return;
  }
  uint16_t cur = temp(k);
  uint16_t obj = kNoReg, idx = kNoReg;
  Expr* readExpr = e->a;
  if (target->k == EK::Member && target->intrinsic == 0 && !(target->field < 0 && !target->cls && target->a->type &&
                                                              baseOf(target->a->type)->t != TT::Object)) {
    obj = temp(K_R);
    genInto(target->a, obj);
    if (target->field >= 0 && target->cls) emit(op3(k, OP_GETF_I, OP_GETF_F, OP_GETF_R), cur, obj, (uint16_t)target->field);
    else {
      D->icNames.push_back(kStr(utf8ToUtf16(target->raw)));
      emit(op3(k, OP_GETN_I, OP_GETN_F, OP_GETN_R), cur, obj, (uint16_t)(D->icNames.size() - 1));
    }
  } else if (target->k == EK::Index) {
    Type* ot = baseOf(target->a->type);
    obj = temp(K_R);
    genInto(target->a, obj);
    if (ot->t == TT::Tuple) {
      emit(op3(k, OP_GETF_I, OP_GETF_F, OP_GETF_R), cur, obj, (uint16_t)target->field);
    } else {
      idx = temp(K(target->b->type));
      genInto(target->b, idx);
      bool ii = target->b->type->t == TT::Int;
      if (ot->t == TT::Bytes) {
        if (ii) { uint16_t t8 = temp(K_I); emit(OP_BGETI, t8, obj, idx); emit(OP_I2F, cur, t8); }
        else emit(OP_BGET, cur, obj, idx);
      } else if (ii) emit(op3(k, OP_AGETI_I, OP_AGETI_F, OP_AGETI_R), cur, obj, idx);
      else emit(op3(k, OP_AGET_I, OP_AGET_F, OP_AGET_R), cur, obj, idx);
    }
  } else {
    genInto(readExpr, cur);
  }
  uint16_t res = temp(k);
  if (bop == "+" && (lt->t == TT::String || (lt->t == TT::Enum && lt->en->isString))) {
    uint16_t r = gen(e->b);
    uint16_t s = toStringReg(r, e->b->type);
    emit(OP_SCAT, res, cur, s);
  } else {
    uint16_t r = gen(e->b);
    binaryOp(bop, lt, e->b->type, res, cur, r, e->loc);
  }
  // write back
  if (obj != kNoReg && target->k == EK::Member) {
    if (target->field >= 0 && target->cls) emit(op3(k, OP_SETF_I, OP_SETF_F, OP_SETF_R), obj, (uint16_t)target->field, res);
    else {
      D->icNames.push_back(kStr(utf8ToUtf16(target->raw)));
      emit(op3(k, OP_SETN_I, OP_SETN_F, OP_SETN_R), obj, (uint16_t)(D->icNames.size() - 1), res);
    }
  } else if (obj != kNoReg && target->k == EK::Index) {
    Type* ot = baseOf(target->a->type);
    if (ot->t == TT::Tuple) emit(op3(k, OP_SETF_I, OP_SETF_F, OP_SETF_R), obj, (uint16_t)target->field, res);
    else {
      bool ii = target->b->type->t == TT::Int;
      if (ot->t == TT::Bytes) {
        if (ii) { uint16_t f = temp(K_F); intToNum(f, idx, target->b->type); idx = f; }
        emit(OP_BSET, obj, idx, res);
      } else if (ii) emit(op3(k, OP_ASETI_I, OP_ASETI_F, OP_ASETI_R), obj, idx, res);
      else emit(op3(k, OP_ASET_I, OP_ASET_F, OP_ASET_R), obj, idx, res);
    }
  } else {
    storeTo(e->a, res);
  }
  if (dst != kNoReg) mov(dst, res, k);
  release(m0);
}

// ======================================================================= operators
void Gen::binaryOp(const std::string& op, Type* lt, Type* rt, uint16_t d, uint16_t a, uint16_t b, Loc l) {
  Type* t = lt;
  if (t->t == TT::Enum && !t->en->isString) t = T.number();
  if (t->t == TT::Number) {
    if (op == "+") emit(OP_ADD_F, d, a, b);
    else if (op == "-") emit(OP_SUB_F, d, a, b);
    else if (op == "*") emit(OP_MUL_F, d, a, b);
    else if (op == "/") emit(OP_DIV_F, d, a, b);
    else if (op == "%") emit(OP_MOD_F, d, a, b);
    else if (op == "**") emit(OP_POW_F, d, a, b);
    else if (op == "&") emit(OP_BAND_F, d, a, b);
    else if (op == "|") emit(OP_BOR_F, d, a, b);
    else if (op == "^") emit(OP_BXOR_F, d, a, b);
    else if (op == "<<") emit(OP_SHL_F, d, a, b);
    else if (op == ">>") emit(OP_SAR_F, d, a, b);
    else if (op == ">>>") emit(OP_SHR_F, d, a, b);
    else if (op == "<") emit(OP_LT_F, d, a, b);
    else if (op == ">") emit(OP_LT_F, d, b, a);
    else if (op == "<=") emit(OP_LE_F, d, a, b);
    else if (op == ">=") emit(OP_LE_F, d, b, a);
    else err(l, "internal error: number operator " + op);
    return;
  }
  if (t->t == TT::Int) {
    bool sgn = isSignedInt(t);
    int bits = intBits(t);
    if (op == "+") { emit(OP_ADD_I, d, a, b); wrapInt(d, d, t); }
    else if (op == "-") { emit(OP_SUB_I, d, a, b); wrapInt(d, d, t); }
    else if (op == "*") { emit(OP_MUL_I, d, a, b); wrapInt(d, d, t); }
    else if (op == "/") { emit(sgn ? OP_DIVS_I : OP_DIVU_I, d, a, b); wrapInt(d, d, t); }
    else if (op == "%") { emit(sgn ? OP_REMS_I : OP_REMU_I, d, a, b); wrapInt(d, d, t); }
    else if (op == "&") emit(OP_AND_I, d, a, b);
    else if (op == "|") emit(OP_OR_I, d, a, b);
    else if (op == "^") emit(OP_XOR_I, d, a, b);
    else if (op == "<<") {
      emit(bits == 64 ? OP_SHL_I : OP_SHL32, d, a, b);
      wrapInt(d, d, t);
    } else if (op == ">>" || op == ">>>") {
      bool logical = op == ">>>" || !sgn;
      if (bits == 64) emit(logical ? OP_SHR_I : OP_SAR_I, d, a, b);
      else if (!logical) { emit(OP_SAR32, d, a, b); wrapInt(d, d, t); }
      else if (!sgn) emit(OP_SHR32, d, a, b);
      else {
        // logical shift of a signed narrow value: operate on its bit pattern
        uint16_t tmp = temp(K_I);
        if (bits == 8) emit(OP_WRAP_U8, tmp, a);
        else if (bits == 16) emit(OP_WRAP_U16, tmp, a);
        else emit(OP_WRAP_U32, tmp, a);
        emit(OP_SHR32, d, tmp, b);
        wrapInt(d, d, t);
      }
    } else if (op == "<") emit(!sgn && bits == 64 ? OP_LTU_I : OP_LT_I, d, a, b);
    else if (op == ">") emit(!sgn && bits == 64 ? OP_LTU_I : OP_LT_I, d, b, a);
    else if (op == "<=") emit(!sgn && bits == 64 ? OP_LEU_I : OP_LE_I, d, a, b);
    else if (op == ">=") emit(!sgn && bits == 64 ? OP_LEU_I : OP_LE_I, d, b, a);
    else err(l, "internal error: int operator " + op);
    return;
  }
  if (t->t == TT::BigInt) {
    static const std::map<std::string, std::string> names = {
        {"+", "add"}, {"-", "sub"}, {"*", "mul"}, {"/", "div"}, {"%", "mod"}, {"**", "pow"},
        {"&", "and"}, {"|", "or"}, {"^", "xor"}, {"<<", "shl"}, {">>", "shr"}};
    auto it = names.find(op);
    if (it != names.end()) {
      callNative("BigInt." + it->second, {a, b}, d, 'R');
      return;
    }
    uint16_t c = temp(K_F), z = temp(K_F);
    callNative("BigInt.cmp", {a, b}, c, 'F');
    loadNum(z, 0);
    if (op == "<") emit(OP_LT_F, d, c, z);
    else if (op == ">") emit(OP_LT_F, d, z, c);
    else if (op == "<=") emit(OP_LE_F, d, c, z);
    else if (op == ">=") emit(OP_LE_F, d, z, c);
    else err(l, "internal error: bigint operator " + op);
    return;
  }
  if (t->t == TT::String || (t->t == TT::Enum && t->en->isString)) {
    if (op == "<") emit(OP_SLT, d, a, b);
    else if (op == ">") emit(OP_SLT, d, b, a);
    else if (op == "<=") emit(OP_SLE, d, a, b);
    else if (op == ">=") emit(OP_SLE, d, b, a);
    else if (op == "+") {
      uint16_t sb = toStringReg(b, rt);
      emit(OP_SCAT, d, a, sb);
    } else err(l, "internal error: string operator " + op);
    return;
  }
  err(l, "internal error: operator '" + op + "' on " + typeName(lt));
}

void Gen::genBinary(Expr* e, uint16_t d) {
  const std::string& op = e->raw;
  if (op == "instanceof") {
    uint16_t a = gen(e->a);
    emit(OP_INSTOF, d, a, (uint16_t)e->cls->classIdx);
    return;
  }
  if (op == "===" || op == "!==" || op == "==" || op == "!=") {
    genEquality(e, d, op[0] == '!');
    return;
  }
  if (e->intrinsic == IN_STR_CONCAT) {
    uint16_t a = gen(e->a);
    uint16_t sa = toStringReg(a, e->a->type);
    uint16_t b = gen(e->b);
    uint16_t sb = toStringReg(b, e->b->type);
    emit(OP_SCAT, d, sa, sb);
    return;
  }
  uint16_t a = gen(e->a);
  uint16_t b = gen(e->b);
  binaryOp(op, e->a->type, e->b->type, d, a, b, e->loc);
}

void Gen::genEquality(Expr* e, uint16_t d, bool negate) {
  Expr* l = strip(e->a);
  Expr* r = strip(e->b);
  bool loose = e->raw.size() == 2;
  auto finish = [&](uint16_t res) {
    if (negate) emit(OP_NOT_B, d, res);
    else mov(d, res, K_I);
  };
  bool lNull = l->type->t == TT::Null || l->type->t == TT::Undefined;
  bool rNull = r->type->t == TT::Null || r->type->t == TT::Undefined;
  if (lNull && rNull) {
    bool eq = loose || l->type == r->type;
    loadInt(d, (eq != negate) ? 1 : 0);
    return;
  }
  if (lNull || rNull) {
    Expr* lit = lNull ? l : r;
    Expr* other = lNull ? r : l;
    uint16_t o = gen(other);
    uint16_t res = temp(K_I);
    if (loose) emit(OP_ISNULLISH, res, o);
    else if (lit->type->t == TT::Null) emit(OP_ISNULL, res, o);
    else emit(OP_ISUNDEF, res, o);
    finish(res);
    return;
  }
  Type* ta = l->type;
  Type* tb = r->type;
  uint16_t a = gen(e->a);
  uint16_t b = gen(e->b);
  uint16_t res = temp(K_I);
  bool boxA = isBoxedOptional(ta), boxB = isBoxedOptional(tb);
  Type* ba = baseOf(ta);
  Type* bb = baseOf(tb);
  if ((boxA && boxB) || (ta->t == TT::Optional && tb->t == TT::Optional) || ba->t == TT::BigInt || bb->t == TT::BigInt) {
    uint16_t xa = a, xb = b;
    if (kindOf(ta) != K_R) { xa = temp(K_R); boxIfNeeded(xa, a, ta, T.optional(ta, false, true)); }
    if (kindOf(tb) != K_R) { xb = temp(K_R); boxIfNeeded(xb, b, tb, T.optional(tb, false, true)); }
    callNative("Value.strictEq", {xa, xb}, res, 'I');
    finish(res);
    return;
  }
  if (boxA || boxB) {
    uint16_t opt = boxA ? a : b;
    uint16_t prim = boxA ? b : a;
    Type* pt = boxA ? tb : ta;
    Label* no = newLabel();
    Label* end = newLabel();
    jump(OP_JNULLISH, opt, no);
    uint16_t u = temp(K(pt));
    emit(kindOf(pt) == K_F ? OP_UNBOX_F : OP_UNBOX_I, u, opt);
    if (kindOf(pt) == K_F) emit(OP_EQ_F, res, u, prim);
    else emit(OP_EQ_I, res, u, prim);
    jump(OP_JMP, 0, end);
    place(no);
    loadInt(res, 0);
    place(end);
    finish(res);
    return;
  }
  bool isStr = ba->t == TT::String || bb->t == TT::String || (ba->t == TT::Enum && ba->en->isString);
  if (isStr) emit(negate ? OP_SNE : OP_SEQ, d, a, b);
  else if (kindOf(ba) == K_F) emit(negate ? OP_NE_F : OP_EQ_F, d, a, b);
  else if (kindOf(ba) == K_I) emit(negate ? OP_NE_I : OP_EQ_I, d, a, b);
  else emit(negate ? OP_NE_R : OP_EQ_R, d, a, b);
}

// ======================================================================= calls
std::vector<uint16_t> Gen::genArgs(std::vector<Expr*>& args, Type* ft, bool restPacked, size_t* nGiven) {
  (void)restPacked;
  std::vector<uint16_t> regs;
  for (Expr* a : args) {
    Expr* x = a->k == EK::Spread ? a->a : a;
    uint16_t r = gen(x);
    regs.push_back(r);
  }
  if (nGiven) *nGiven = regs.size();
  if (ft) {
    for (size_t i = regs.size(); i < ft->params.size(); i++) {
      Type* pt = ft->params[i];
      uint16_t r = temp(K(pt));
      if (pt->t == TT::Optional && pt->hasUndef) emit(OP_LOADUNDEF, r);
      else if (ft->restParam && i + 1 == ft->params.size()) emit(OP_NEWARR, r, vtOf(pt->elem), 0);
      else loadZero(r, K(pt));
      regs.push_back(r);
    }
  }
  return regs;
}

void Gen::callFunc(FuncSym* f, uint16_t dst, const std::vector<uint16_t>& args0, Type* ft, std::vector<Expr*>* argExprs) {
  (void)ft;
  (void)argExprs;
  std::vector<uint16_t> args = args0;
  if (f->hasDefaults) {
    uint16_t ac = temp(K_I);
    loadInt(ac, (int64_t)(f->isMethod || f->isCtor ? args.size() - 1 : args.size()));
    // argc = number of explicitly given arguments (computed by caller below)
    args.push_back(ac);
  }
  Kind rk = kindOf(f->type->ret);
  uint16_t d = (rk == K_V) ? kNoReg : dst;
  if (rk != K_V && dst == kNoReg) d = kNoReg;
  emit(OP_CALL, d, (uint16_t)f->funcIdx, (uint16_t)args.size());
  emitRegList(args);
}

void Gen::callClosure(uint16_t clo, Type* ft, const std::vector<uint16_t>& args, uint16_t dst) {
  std::string sg;
  for (Type* p : ft->params) sg += kindChar(K(p));
  sg += '>';
  Kind rk = kindOf(ft->ret);
  sg += rk == K_V ? 'V' : kindChar(rk);
  uint32_t si = sig(sg);
  emit(OP_CALLV, rk == K_V ? kNoReg : dst, clo, (uint16_t)args.size());
  emitWord(si);
  emitRegList(args);
}

void Gen::genCall(Expr* e, uint16_t dst) {
  Expr* callee = strip(e->a);
  Kind rk = e->type ? kindOf(e->type) : K_V;
  auto retChar = [&]() { return rk == K_V ? 'V' : kindChar(rk); };
  // optional chaining on the receiver: obj?.m()
  if (e->optional && callee->k == EK::Member && callee->optional) {
    uint16_t recv = temp(K_R);
    genInto(callee->a, recv);
    Label* nul = newLabel();
    Label* end = newLabel();
    jump(OP_JNULLISH, recv, nul);
    Expr* savedObj = callee->a;
    Type* bt = baseOf(savedObj->type);
    uint16_t recvBase = recv;
    if (isBoxedOptional(savedObj->type)) {
      recvBase = temp(K(bt));
      emit(kindOf(bt) == K_F ? OP_UNBOX_F : OP_UNBOX_I, recvBase, recv);
    }
    callee->a = regExpr(recvBase, bt, savedObj->loc);
    e->optional = false;
    Type* savedT = e->type;
    Type* innerT = e->innerType ? e->innerType : baseOf(savedT);
    e->type = innerT;
    Kind ik = kindOf(innerT);
    if (ik == K_V || dst == kNoReg) {
      genCall(e, kNoReg);
    } else {
      uint16_t v = temp(K(innerT));
      genCall(e, v);
      boxIfNeeded(dst, v, innerT, savedT);
    }
    e->type = savedT;
    e->optional = true;
    callee->a = savedObj;
    jump(OP_JMP, 0, end);
    place(nul);
    if (dst != kNoReg && kindOf(savedT) != K_V) emit(OP_LOADUNDEF, dst);
    place(end);
    return;
  }
  switch (e->intrinsic) {
    case IN_DIRECT: {
      FuncSym* f = e->callee;
      if (f->isNative) {
        std::vector<uint16_t> args;
        for (Expr* a : e->list) args.push_back(gen(a));
        callNative(f->name, args, dst, retChar());
        return;
      }
      if (f->selfVar && !f->captures.empty()) {
        uint16_t clo = temp(K_R);
        readVar(f->selfVar, clo, f->selfVar->type);
        size_t n;
        std::vector<uint16_t> args = genArgs(e->list, f->type, e->bval, &n);
        callClosure(clo, f->type, args, dst);
        return;
      }
      size_t n;
      std::vector<uint16_t> args = genArgs(e->list, f->type, e->bval, &n);
      if (f->hasDefaults) {
        uint16_t ac = temp(K_I);
        loadInt(ac, (int64_t)n);
        args.push_back(ac);
      }
      emit(OP_CALL, rk == K_V ? kNoReg : dst, (uint16_t)f->funcIdx, (uint16_t)args.size());
      emitRegList(args);
      return;
    }
    case IN_SUPER_CTOR:
    case IN_SUPER_METHOD: {
      FuncSym* f = e->callee;
      uint16_t self = temp(K_R);
      readVar(e->var, self, e->var->type);
      size_t n;
      std::vector<uint16_t> args = genArgs(e->list, f->type, e->bval, &n);
      args.insert(args.begin(), self);
      if (f->hasDefaults) {
        uint16_t ac = temp(K_I);
        loadInt(ac, (int64_t)n);
        args.push_back(ac);
      }
      emit(OP_CALL, rk == K_V ? kNoReg : dst, (uint16_t)f->funcIdx, (uint16_t)args.size());
      emitRegList(args);
      return;
    }
    case IN_METHOD: {
      FuncSym* f = e->callee;
      uint16_t obj = gen(callee->a);
      size_t n;
      std::vector<uint16_t> args = genArgs(e->list, f->type, e->bval, &n);
      if (f->hasDefaults) {
        uint16_t ac = temp(K_I);
        loadInt(ac, (int64_t)n);
        args.push_back(ac);
      }
      uint32_t si = sig(sigOf(f));
      emit(OP_CALLM, rk == K_V ? kNoReg : dst, obj, (uint16_t)args.size());
      emitWord((uint64_t)(uint32_t)e->vslot | ((uint64_t)si << 32));
      emitRegList(args);
      return;
    }
    case IN_IFACE_METHOD: {
      Type* ft = callee->type;  // method signature
      uint16_t obj = gen(callee->a);
      size_t n;
      std::vector<uint16_t> args = genArgs(e->list, ft, e->bval, &n);
      std::string sg = "R";
      for (Type* p : ft->params) sg += kindChar(K(p));
      sg += '>';
      sg += rk == K_V ? 'V' : kindChar(rk);
      uint32_t si = sig(sg);
      D->icNames.push_back(kStr(utf8ToUtf16(callee->raw)));
      uint32_t ic = (uint32_t)(D->icNames.size() - 1);
      emit(OP_CALLI, rk == K_V ? kNoReg : dst, obj, (uint16_t)args.size());
      emitWord((uint64_t)ic | ((uint64_t)si << 32));
      emitRegList(args);
      return;
    }
    case IN_FIELD_CALL: {
      Type* ft = callee->type;
      uint16_t obj = gen(callee->a);
      uint16_t clo = temp(K_R);
      if (callee->field >= 0 && callee->cls) emit(OP_GETF_R, clo, obj, (uint16_t)callee->field);
      else {
        D->icNames.push_back(kStr(utf8ToUtf16(callee->raw)));
        emit(OP_GETN_R, clo, obj, (uint16_t)(D->icNames.size() - 1));
      }
      std::vector<uint16_t> args = genArgs(e->list, ft, e->bval, nullptr);
      callClosure(clo, ft, args, dst);
      return;
    }
    case IN_CLOSURE_CALL: {
      Type* ft = e->a->type;
      if (ft->t == TT::Optional) ft = ft->elem;
      uint16_t clo = gen(e->a);
      if (e->optional) {
        Label* nul = newLabel();
        Label* end = newLabel();
        jump(OP_JNULLISH, clo, nul);
        std::vector<uint16_t> args = genArgs(e->list, ft, e->bval, nullptr);
        if (kindOf(ft->ret) == K_V || dst == kNoReg) callClosure(clo, ft, args, kNoReg);
        else {
          uint16_t v = temp(K(ft->ret));
          callClosure(clo, ft, args, v);
          boxIfNeeded(dst, v, ft->ret, e->type);
        }
        jump(OP_JMP, 0, end);
        place(nul);
        if (dst != kNoReg && kindOf(ft->ret) != K_V) emit(OP_LOADUNDEF, dst);
        place(end);
        return;
      }
      std::vector<uint16_t> args = genArgs(e->list, ft, e->bval, nullptr);
      callClosure(clo, ft, args, dst);
      return;
    }
    case IN_NATIVE:
    case IN_NATIVE_NORECV: {
      if (e->native == "Array.concat" && e->list.size() > 1) {
        uint16_t acc = temp(K_R);
        uint16_t first = gen(callee->a);
        mov(acc, first, K_R);
        for (Expr* a : e->list) {
          size_t m0 = mark();
          uint16_t v = gen(a);
          callNative("Array.concat", {acc, v}, acc, 'R');
          release(m0);
        }
        if (dst != kNoReg) mov(dst, acc, K_R);
        return;
      }
      std::vector<uint16_t> args;
      if (e->intrinsic == IN_NATIVE) args.push_back(gen(callee->a));
      for (Expr* a : e->list) args.push_back(gen(a));
      callNative(e->native, args, dst, retChar());
      return;
    }
    case IN_TO_INT: {
      Expr* a = e->list[0];
      Type* at = a->type;
      Type* tt = e->type;
      uint16_t d = dst == kNoReg ? temp(K_I) : dst;
      if (at->t == TT::Int) {
        uint16_t r = gen(a);
        wrapInt(d, r, tt);
      } else if (at->t == TT::BigInt) {
        uint16_t r = gen(a);
        callNative("BigInt.toU64", {r}, d, 'I');
        wrapInt(d, d, tt);
      } else if (at->t == TT::Bool) {
        uint16_t r = gen(a);
        mov(d, r, K_I);
      } else {
        uint16_t r = gen(a);
        emit(OP_F2I, d, r);
        wrapInt(d, d, tt);
      }
      return;
    }
    case IN_INTOP: {
      const std::string& op = e->raw;
      Type* t = e->type;
      uint16_t d = dst == kNoReg ? temp(K_I) : dst;
      uint16_t a = gen(e->list[0]);
      uint16_t b = e->list.size() > 1 ? gen(e->list[1]) : kNoReg;
      if (op == "rotl32") emit(OP_ROTL32, d, a, b);
      else if (op == "rotr32") emit(OP_ROTR32, d, a, b);
      else if (op == "rotl64") emit(OP_ROTL64, d, a, b);
      else if (op == "rotr64") emit(OP_ROTR64, d, a, b);
      else if (op == "clz32") emit(OP_CLZ32, d, a);
      else if (op == "clz64") emit(OP_CLZ64, d, a);
      else if (op == "ctz32") emit(OP_CTZ32, d, a);
      else if (op == "ctz64") emit(OP_CTZ64, d, a);
      else if (op == "popcnt") {
        uint16_t m = temp(K_I);
        int bits = intBits(t);
        if (bits == 8) emit(OP_WRAP_U8, m, a);
        else if (bits == 16) emit(OP_WRAP_U16, m, a);
        else if (bits == 32) emit(OP_WRAP_U32, m, a);
        else mov(m, a, K_I);
        emit(OP_POPCNT, d, m);
        return;
      } else if (op == "bswap16") emit(OP_BSWAP16, d, a);
      else if (op == "bswap32") emit(OP_BSWAP32, d, a);
      else if (op == "bswap64") emit(OP_BSWAP64, d, a);
      else if (op == "mulhi64") emit(OP_MULHIU, d, a, b);
      else if (op == "imulhi64") emit(OP_MULHIS, d, a, b);
      wrapInt(d, d, t);
      return;
    }
    case IN_NUMBER: {
      Expr* a = e->list[0];
      if (e->raw == "identity") { genInto(a, dst); return; }
      Type* at = a->type;
      uint16_t d = dst == kNoReg ? temp(K_F) : dst;
      uint16_t r = gen(a);
      if (at->t == TT::String) callNative("Global.Number", {r}, d, 'F');
      else if (at->t == TT::Bool) emit(OP_I2F, d, r);
      else if (at->t == TT::Int) intToNum(d, r, at);
      else if (at->t == TT::BigInt) callNative("BigInt.toNumber", {r}, d, 'F');
      else mov(d, r, K_F);
      return;
    }
    case IN_STRING: {
      uint16_t r = gen(e->list[0]);
      uint16_t s = toStringReg(r, e->list[0]->type);
      if (dst != kNoReg) mov(dst, s, K_R);
      return;
    }
    case IN_BOOLEAN: {
      uint16_t t = genTruthy(e->list[0]);
      if (dst != kNoReg) mov(dst, t, K_I);
      return;
    }
    case IN_BIGINT: {
      Expr* a = e->list[0];
      Type* at = a->type;
      uint16_t d = dst == kNoReg ? temp(K_R) : dst;
      uint16_t r = gen(a);
      if (at->t == TT::Number) callNative("BigInt.fromNumber", {r}, d, 'R');
      else if (at->t == TT::String) callNative("BigInt.fromString", {r}, d, 'R');
      else if (at->t == TT::Int) callNative(at->vt == VT_U64 ? "BigInt.fromU64" : "BigInt.fromI64", {r}, d, 'R');
      else mov(d, r, K_R);
      return;
    }
    case IN_TOSTRING: {
      uint16_t r = gen(callee->a);
      uint16_t s = toStringReg(r, baseOf(callee->a->type));
      if (dst != kNoReg) mov(dst, s, K_R);
      return;
    }
    case IN_ARR_PUSH: {
      uint16_t arr = gen(callee->a);
      Type* et = baseOf(callee->a->type)->elem;
      for (Expr* a : e->list) {
        size_t m0 = mark();
        uint16_t v = gen(a);
        emit(op3(K(et), OP_APUSH_I, OP_APUSH_F, OP_APUSH_R), arr, v);
        release(m0);
      }
      if (dst != kNoReg) emit(OP_ALEN, dst, arr);
      return;
    }
    case IN_STR_CONCAT: {
      uint16_t acc = temp(K_R);
      uint16_t s0 = gen(callee->a);
      mov(acc, s0, K_R);
      for (Expr* a : e->list) {
        size_t m0 = mark();
        uint16_t v = gen(a);
        uint16_t s = toStringReg(v, a->type);
        emit(OP_SCAT, acc, acc, s);
        release(m0);
      }
      if (dst != kNoReg) mov(dst, acc, K_R);
      return;
    }
    case IN_CONSOLE_LOG:
      genConsoleLog(e);
      return;
    case IN_MATH_MINMAX:
    case IN_HYPOT: {
      bool isMin = e->raw == "min";
      bool anySpread = false;
      for (Expr* a : e->list) if (a->k == EK::Spread) anySpread = true;
      uint16_t d = dst == kNoReg ? temp(K_F) : dst;
      if (e->intrinsic == IN_HYPOT || anySpread) {
        uint16_t arr = temp(K_R);
        emit(OP_NEWARR, arr, VT_NUMBER, (uint16_t)std::min<size_t>(e->list.size(), 1000));
        for (Expr* a : e->list) {
          size_t m0 = mark();
          if (a->k == EK::Spread) {
            uint16_t src = gen(a->a);
            uint16_t n = temp(K_F);
            callNative("Array.pushArray", {arr, src}, n, 'F');
          } else {
            uint16_t v = gen(a);
            emit(OP_APUSH_F, arr, v);
          }
          release(m0);
        }
        std::string nm = e->intrinsic == IN_HYPOT ? "Math.hypotArray" : isMin ? "Math.minArray" : "Math.maxArray";
        callNative(nm, {arr}, d, 'F');
        return;
      }
      if (e->list.empty()) { loadNum(d, isMin ? INFINITY : -INFINITY); return; }
      uint16_t acc = temp(K_F);
      genInto(e->list[0], acc);
      if (e->list.size() == 1) {
        uint16_t inf = temp(K_F);
        loadNum(inf, isMin ? INFINITY : -INFINITY);
        callNative(isMin ? "Math.min" : "Math.max", {acc, inf}, acc, 'F');
      }
      for (size_t i = 1; i < e->list.size(); i++) {
        size_t m0 = mark();
        uint16_t v = gen(e->list[i]);
        callNative(isMin ? "Math.min" : "Math.max", {acc, v}, acc, 'F');
        release(m0);
      }
      mov(d, acc, K_F);
      return;
    }
    case IN_ABORT: {
      uint16_t m = gen(e->list[0]);
      emit(OP_ABORT, m);
      return;
    }
    case IN_ARR_MAP: case IN_ARR_FILTER: case IN_ARR_FOREACH: case IN_ARR_REDUCE: case IN_ARR_REDUCE_RIGHT:
    case IN_ARR_SOME: case IN_ARR_EVERY: case IN_ARR_FIND: case IN_ARR_FINDINDEX: case IN_ARR_FINDLAST:
    case IN_ARR_FINDLASTINDEX: case IN_ARR_FROM_LENGTH: case IN_MAP_FOREACH: case IN_SET_FOREACH: case IN_BYTES_FOREACH:
    case IN_ARRAY_FROM:
      genCallbackLoop(e, dst);
      return;
    default:
      err(e->loc, "internal error: unsupported call lowering " + std::to_string(e->intrinsic));
  }
}

void Gen::genConsoleLog(Expr* e) {
  uint16_t arr = temp(K_R);
  emit(OP_NEWARR, arr, VT_REF, (uint16_t)std::min<size_t>(e->list.size(), 1000));
  for (Expr* a : e->list) {
    size_t m0 = mark();
    uint16_t v = gen(a);
    Type* t = a->type;
    if (kindOf(t) != K_R) {
      uint16_t b = temp(K_R);
      if (kindOf(t) == K_F) emit(OP_BOX_F, b, v);
      else emit(OP_BOX_I, b, v, vtOf(t));
      v = b;
    }
    emit(OP_APUSH_R, arr, v);
    release(m0);
  }
  callNative(e->raw == "error" ? "console.error" : "console.log", {arr}, kNoReg, 'V');
}

void Gen::spreadInto(uint16_t arr, Expr* sp, Type* elemT) {
  (void)elemT;
  size_t m0 = mark();
  Type* st = sp->type;
  uint16_t src = gen(sp->a);
  uint16_t n = temp(K_F);
  uint16_t tmpArr = src;
  if (st->t == TT::Set) { tmpArr = temp(K_R); callNative("Map.keys", {src}, tmpArr, 'R'); }
  else if (st->t == TT::String) { tmpArr = temp(K_R); callNative("String.chars", {src}, tmpArr, 'R'); }
  else if (st->t == TT::Bytes) { tmpArr = temp(K_R); callNative("Bytes.toArray", {src}, tmpArr, 'R'); }
  else if (st->t == TT::Map) {
    ClassSym* rc = C.tupleRecord(T.tuple({st->key, st->val}));
    uint16_t lc = temp(K_I);
    loadInt(lc, rc->classIdx);
    tmpArr = temp(K_R);
    uint16_t cidx = temp(K_F);
    emit(OP_I2F, cidx, lc);
    callNative("Map.entries", {src, cidx}, tmpArr, 'R');
  }
  callNative("Array.pushArray", {arr, tmpArr}, n, 'F');
  release(m0);
}

void Gen::genArrayLit(Expr* e, uint16_t d) {
  if (e->cls && e->type->t == TT::Tuple) {
    uint16_t obj = temp(K_R);
    emit(OP_NEW, obj, (uint16_t)e->cls->classIdx);
    for (size_t i = 0; i < e->list.size(); i++) {
      size_t m0 = mark();
      uint16_t v = gen(e->list[i]);
      Type* ft = e->cls->fields[i].type;
      emit(op3(K(ft), OP_SETF_I, OP_SETF_F, OP_SETF_R), obj, (uint16_t)i, v);
      release(m0);
    }
    mov(d, obj, K_R);
    return;
  }
  Type* et = e->type->elem;
  uint16_t arr = temp(K_R);
  emit(OP_NEWARR, arr, vtOf(et), (uint16_t)std::min<size_t>(e->list.size(), 65535));
  for (Expr* x : e->list) {
    if (x->k == EK::Spread) { spreadInto(arr, x, et); continue; }
    size_t m0 = mark();
    uint16_t v = gen(x);
    emit(op3(K(et), OP_APUSH_I, OP_APUSH_F, OP_APUSH_R), arr, v);
    release(m0);
  }
  mov(d, arr, K_R);
}

void Gen::genObjectLit(Expr* e, uint16_t d) {
  ClassSym* c = e->cls;
  uint16_t obj = temp(K_R);
  emit(OP_NEW, obj, (uint16_t)c->classIdx);
  std::vector<bool> set(c->fields.size(), false);
  for (auto& p : e->props) {
    int fi = c->fieldIdx[p.key];
    size_t m0 = mark();
    uint16_t v = gen(p.value);
    emit(op3(K(c->fields[fi].type), OP_SETF_I, OP_SETF_F, OP_SETF_R), obj, (uint16_t)fi, v);
    set[fi] = true;
    release(m0);
  }
  for (size_t i = 0; i < c->fields.size(); i++) {
    if (set[i]) continue;
    size_t m0 = mark();
    uint16_t u = temp(K_R);
    emit(OP_LOADUNDEF, u);
    if (K(c->fields[i].type) == K_R) emit(OP_SETF_R, obj, (uint16_t)i, u);
    release(m0);
  }
  mov(d, obj, K_R);
}

// Array.prototype higher-order methods and friends, lowered to loops with closure calls
void Gen::genCallbackLoop(Expr* e, uint16_t dst) {
  Expr* callee = strip(e->a);
  int in = e->intrinsic;
  Kind rk = e->type ? kindOf(e->type) : K_V;
  uint16_t d = (dst == kNoReg && rk != K_V) ? temp(K(e->type)) : dst;
  if (in == IN_ARRAY_FROM) {
    // Array.from(x) / map.entries()
    Expr* src = e->list.empty() ? callee->a : e->list[0];
    Type* st = baseOf(src->type);
    uint16_t s = gen(src);
    if (st->t == TT::Array) {
      uint16_t z = temp(K_F), inf = temp(K_F);
      loadNum(z, 0);
      loadNum(inf, INFINITY);
      callNative("Array.slice", {s, z, inf}, d, 'R');
    } else if (st->t == TT::Set) callNative("Map.keys", {s}, d, 'R');
    else if (st->t == TT::String) callNative("String.chars", {s}, d, 'R');
    else if (st->t == TT::Bytes) callNative("Bytes.toArray", {s}, d, 'R');
    else if (st->t == TT::Map) {
      ClassSym* rc = e->cls ? e->cls : C.tupleRecord(T.tuple({st->key, st->val}));
      uint16_t cidx = temp(K_F);
      loadNum(cidx, rc->classIdx);
      callNative("Map.entries", {s, cidx}, d, 'R');
    } else err(e->loc, "internal error: Array.from source");
    return;
  }
  if (in == IN_ARR_FROM_LENGTH) {
    Expr* lenE = strip(e->list[0])->props[0].value;
    Type* ft = e->list[1]->type;
    uint16_t nF = gen(lenE);
    uint16_t n = temp(K_I);
    emit(OP_F2I, n, nF);
    uint16_t clo = temp(K_R);
    genInto(e->list[1], clo);
    uint16_t res = temp(K_R);
    emit(OP_NEWARR, res, vtOf(ft->ret), 0);
    uint16_t i = temp(K_I), c = temp(K_I), u = temp(K_R), fi = temp(K_F), v = temp(K(ft->ret));
    loadInt(i, 0);
    emit(OP_LOADUNDEF, u);
    Label* top = newLabel();
    Label* end = newLabel();
    place(top);
    emit(OP_LT_I, c, i, n);
    jump(OP_JF, c, end);
    emit(OP_I2F, fi, i);
    std::vector<uint16_t> args;
    if (ft->params.size() > 0) args.push_back(u);
    if (ft->params.size() > 1) args.push_back(fi);
    callClosure(clo, ft, args, v);
    emit(op3(K(ft->ret), OP_APUSH_I, OP_APUSH_F, OP_APUSH_R), res, v);
    emit(OP_ADDI_I, i, i, 1);
    jump(OP_JMP, 0, top);
    place(end);
    mov(d, res, K_R);
    return;
  }
  Type* rt = baseOf(callee->a->type);
  uint16_t coll = temp(K_R);
  genInto(callee->a, coll);
  Expr* cbE = e->list.size() == 2 && (in == IN_ARR_REDUCE || in == IN_ARR_REDUCE_RIGHT) ? e->list[0] : e->list[0];
  Type* ft = cbE->type;
  uint16_t clo = temp(K_R);
  genInto(cbE, clo);
  uint16_t i = temp(K_I), c = temp(K_I), n = temp(K_I);
  Label* top = newLabel();
  Label* cont = newLabel();
  Label* end = newLabel();
  Label* found = newLabel();
  size_t np = ft->params.size();
  if (in == IN_MAP_FOREACH || in == IN_SET_FOREACH) {
    Type* kt = rt->t == TT::Map ? rt->key : rt->elem;
    Type* vt = rt->t == TT::Map ? rt->val : rt->elem;
    uint16_t zero = temp(K_I), k = temp(K(kt)), v = temp(K(vt));
    loadInt(i, 0);
    loadInt(zero, 0);
    place(top);
    callNative("Map.next", {coll, i}, i, 'I');
    emit(OP_LT_I, c, i, zero);
    jump(OP_JT, c, end);
    callNative("Map.keyAt" + kindSuffixOf(kt), {coll, i}, k, kindChar(K(kt)));
    if (rt->t == TT::Map) callNative("Map.valAt" + kindSuffixOf(vt), {coll, i}, v, kindChar(K(vt)));
    else mov(v, k, K(kt));
    std::vector<uint16_t> args;
    if (np > 0) args.push_back(v);
    if (np > 1) args.push_back(k);
    if (np > 2) args.push_back(coll);
    callClosure(clo, ft, args, kindOf(ft->ret) == K_V ? kNoReg : temp(K(ft->ret)));
    emit(OP_ADDI_I, i, i, 1);
    jump(OP_JMP, 0, top);
    place(end);
    return;
  }
  bool isBytes = rt->t == TT::Bytes;
  Type* et = isBytes ? T.number() : rt->elem;
  bool reverse = in == IN_ARR_REDUCE_RIGHT || in == IN_ARR_FINDLAST || in == IN_ARR_FINDLASTINDEX;
  emit(isBytes ? OP_BLENI : OP_ALENI, n, coll);
  uint16_t res = kNoReg;
  uint16_t acc = kNoReg;
  if (in == IN_ARR_MAP || in == IN_ARR_FILTER) {
    res = temp(K_R);
    Type* outE = baseOf(e->type)->elem;
    emit(OP_NEWARR, res, vtOf(outE), 0);
  }
  if (in == IN_ARR_REDUCE || in == IN_ARR_REDUCE_RIGHT) {
    acc = temp(K(e->type));
    if (e->list.size() == 2) {
      genInto(e->list[1], acc);
      loadInt(i, reverse ? 0 : 0);
      if (reverse) emit(OP_ADDI_I, i, n, (uint16_t)(int16_t)-1);
    } else {
      Label* ok = newLabel();
      uint16_t z = temp(K_I);
      loadInt(z, 0);
      emit(OP_LT_I, c, z, n);
      jump(OP_JT, c, ok);
      uint16_t msg = temp(K_R);
      loadStr(msg, u"Reduce of empty array with no initial value");
      emit(OP_ABORT, msg);
      place(ok);
      if (reverse) {
        emit(OP_ADDI_I, i, n, (uint16_t)(int16_t)-1);
        emit(op3(K(et), OP_AGETI_I, OP_AGETI_F, OP_AGETI_R), acc, coll, i);
        emit(OP_ADDI_I, i, i, (uint16_t)(int16_t)-1);
      } else {
        emit(op3(K(et), OP_AGETI_I, OP_AGETI_F, OP_AGETI_R), acc, coll, z);
        loadInt(i, 1);
      }
    }
  } else if (reverse) {
    emit(OP_ADDI_I, i, n, (uint16_t)(int16_t)-1);
  } else {
    loadInt(i, 0);
  }
  place(top);
  if (reverse) {
    uint16_t z = temp(K_I);
    loadInt(z, 0);
    emit(OP_LT_I, c, i, z);
    jump(OP_JT, c, end);
  } else {
    emit(OP_LT_I, c, i, n);
    jump(OP_JF, c, end);
    // the array may have shrunk
    uint16_t cur = temp(K_I);
    emit(isBytes ? OP_BLENI : OP_ALENI, cur, coll);
    emit(OP_LT_I, c, i, cur);
    jump(OP_JF, c, end);
  }
  uint16_t v = temp(K(et));
  if (isBytes) {
    uint16_t bi = temp(K_I);
    emit(OP_BGETI, bi, coll, i);
    emit(OP_I2F, v, bi);
  } else {
    emit(op3(K(et), OP_AGETI_I, OP_AGETI_F, OP_AGETI_R), v, coll, i);
  }
  uint16_t fi = temp(K_F);
  emit(OP_I2F, fi, i);
  std::vector<uint16_t> args;
  if (in == IN_ARR_REDUCE || in == IN_ARR_REDUCE_RIGHT) {
    if (np > 0) args.push_back(acc);
    if (np > 1) args.push_back(v);
    if (np > 2) args.push_back(fi);
    if (np > 3) args.push_back(coll);
  } else {
    if (np > 0) args.push_back(v);
    if (np > 1) args.push_back(fi);
    if (np > 2) args.push_back(coll);
  }
  Kind cr = kindOf(ft->ret);
  uint16_t r = cr == K_V ? kNoReg : temp(K(ft->ret));
  callClosure(clo, ft, args, r);
  switch (in) {
    case IN_ARR_MAP: emit(op3(K(ft->ret), OP_APUSH_I, OP_APUSH_F, OP_APUSH_R), res, r); break;
    case IN_ARR_FILTER: {
      Label* skip = newLabel();
      jump(OP_JF, r, skip);
      emit(op3(K(et), OP_APUSH_I, OP_APUSH_F, OP_APUSH_R), res, v);
      place(skip);
      break;
    }
    case IN_ARR_SOME: case IN_ARR_FIND: case IN_ARR_FINDINDEX: case IN_ARR_FINDLAST: case IN_ARR_FINDLASTINDEX:
      jump(OP_JT, r, found);
      break;
    case IN_ARR_EVERY: jump(OP_JF, r, found); break;
    case IN_ARR_REDUCE: case IN_ARR_REDUCE_RIGHT: mov(acc, r, K(e->type)); break;
    default: break;
  }
  place(cont);
  emit(OP_ADDI_I, i, i, (uint16_t)(int16_t)(reverse ? -1 : 1));
  jump(OP_JMP, 0, top);
  // results
  Label* done = newLabel();
  place(end);
  switch (in) {
    case IN_ARR_MAP: case IN_ARR_FILTER: if (d != kNoReg) mov(d, res, K_R); break;
    case IN_ARR_SOME: if (d != kNoReg) loadInt(d, 0); break;
    case IN_ARR_EVERY: if (d != kNoReg) loadInt(d, 1); break;
    case IN_ARR_FIND: case IN_ARR_FINDLAST: if (d != kNoReg) emit(OP_LOADUNDEF, d); break;
    case IN_ARR_FINDINDEX: case IN_ARR_FINDLASTINDEX: if (d != kNoReg) loadNum(d, -1); break;
    case IN_ARR_REDUCE: case IN_ARR_REDUCE_RIGHT: if (d != kNoReg) mov(d, acc, K(e->type)); break;
    default: break;
  }
  jump(OP_JMP, 0, done);
  place(found);
  switch (in) {
    case IN_ARR_SOME: if (d != kNoReg) loadInt(d, 1); break;
    case IN_ARR_EVERY: if (d != kNoReg) loadInt(d, 0); break;
    case IN_ARR_FIND: case IN_ARR_FINDLAST: if (d != kNoReg) boxIfNeeded(d, v, et, e->type); break;
    case IN_ARR_FINDINDEX: case IN_ARR_FINDLASTINDEX: if (d != kNoReg) mov(d, fi, K_F); break;
    default: break;
  }
  place(done);
}

void Gen::genNew(Expr* e, uint16_t d) {
  if (e->cls && e->callee) {
    FuncSym* ctor = e->callee;
    uint16_t obj = temp(K_R);
    emit(OP_NEW, obj, (uint16_t)e->cls->classIdx);
    size_t n;
    std::vector<uint16_t> args = genArgs(e->list, ctor->type, e->bval, &n);
    args.insert(args.begin(), obj);
    if (ctor->hasDefaults) {
      uint16_t ac = temp(K_I);
      loadInt(ac, (int64_t)n);
      args.push_back(ac);
    }
    emit(OP_CALL, kNoReg, (uint16_t)ctor->funcIdx, (uint16_t)args.size());
    emitRegList(args);
    mov(d, obj, K_R);
    return;
  }
  if (e->intrinsic == IN_CONST_NUM) { emit(OP_LOADNULL, d); return; }
  Type* t = e->type;
  if (e->intrinsic == IN_NEW_ARRAY) {
    if (e->list.empty()) { emit(OP_NEWARR, d, vtOf(t->elem), 0); return; }
    uint16_t n = gen(e->list[0]);
    uint16_t vt = temp(K_F);
    loadNum(vt, vtOf(t->elem));
    callNative("Array.newLen", {n, vt}, d, 'R');
    return;
  }
  const std::string& nm = e->native;
  if (nm == "Error.new") { genInto(e->list[0], d); return; }
  std::vector<uint16_t> args;
  for (Expr* a : e->list) args.push_back(gen(a));
  auto num = [&](double v) {
    uint16_t r = temp(K_F);
    loadNum(r, v);
    return r;
  };
  if (nm == "Map.new" || nm == "Map.fromEntries") {
    args.push_back(num(vtOf(t->key)));
    args.push_back(num(vtOf(t->val)));
    args.push_back(num(keyModeOf(t->key)));
  } else if (nm == "Set.new" || nm == "Set.fromArray") {
    args.push_back(num(vtOf(t->elem)));
    args.push_back(num(keyModeOf(t->elem)));
  }
  callNative(nm, args, d, 'R');
}

void Gen::genMember(Expr* e, uint16_t d) {
  if (e->optional) {
    uint16_t obj = temp(K_R);
    genInto(e->a, obj);
    Label* nul = newLabel();
    Label* end = newLabel();
    jump(OP_JNULLISH, obj, nul);
    Expr* saved = e->a;
    Type* bt = baseOf(saved->type);
    uint16_t base = obj;
    if (isBoxedOptional(saved->type)) {
      base = temp(K(bt));
      emit(kindOf(bt) == K_F ? OP_UNBOX_F : OP_UNBOX_I, base, obj);
    }
    e->a = regExpr(base, bt, saved->loc);
    e->optional = false;
    Type* savedT = e->type;
    Type* innerT = e->innerType ? e->innerType : baseOf(savedT);
    e->type = innerT;
    uint16_t v = temp(K(innerT));
    genMember(e, v);
    boxIfNeeded(d, v, innerT, savedT);
    e->type = savedT;
    e->optional = true;
    e->a = saved;
    jump(OP_JMP, 0, end);
    place(nul);
    emit(OP_LOADUNDEF, d);
    place(end);
    return;
  }
  switch (e->intrinsic) {
    case IN_CONST_NUM: loadNum(d, e->num); return;
    case IN_CONST_STR: loadStr(d, e->str); return;
    case IN_STATIC_FIELD: readVar(e->var, d, e->type); return;
    case IN_DIRECT: genClosure(e->callee, d); return;
    case IN_ARR_LEN: {
      uint16_t obj = gen(e->a);
      Type* ot = baseOf(e->a->type);
      if (ot->t == TT::String) emit(OP_SLEN, d, obj);
      else if (ot->t == TT::Array) emit(OP_ALEN, d, obj);
      else emit(OP_BLEN, d, obj);
      return;
    }
    case IN_NATIVE: {
      uint16_t obj = gen(e->a);
      if (e->native == "Error.message") { mov(d, obj, K_R); return; }
      callNative(e->native, {obj}, d, kindChar(K(e->type)));
      return;
    }
    default: break;
  }
  uint16_t obj = gen(e->a);
  Type* ot = baseOf(e->a->type);
  Type* fieldT;
  if (e->field >= 0 && e->cls) fieldT = e->cls->fields[e->field].type;
  else fieldT = ot->shape->findProp(e->raw)->type;
  Kind fk = K(fieldT);
  auto load = [&](uint16_t into) {
    if (e->field >= 0 && e->cls) emit(op3(fk, OP_GETF_I, OP_GETF_F, OP_GETF_R), into, obj, (uint16_t)e->field);
    else {
      D->icNames.push_back(kStr(utf8ToUtf16(e->raw)));
      emit(op3(fk, OP_GETN_I, OP_GETN_F, OP_GETN_R), into, obj, (uint16_t)(D->icNames.size() - 1));
    }
  };
  if (isBoxedOptional(fieldT) && e->type != fieldT && kindOf(e->type) != K_R) {
    uint16_t v = temp(K_R);
    load(v);
    emit(kindOf(e->type) == K_F ? OP_UNBOX_F : OP_UNBOX_I, d, v);
    return;
  }
  load(d);
}

void Gen::genIndex(Expr* e, uint16_t d) {
  Type* ot = baseOf(e->a->type);
  uint16_t obj = gen(e->a);
  if (ot->t == TT::Tuple) {
    emit(op3(K(e->type), OP_GETF_I, OP_GETF_F, OP_GETF_R), d, obj, (uint16_t)e->field);
    return;
  }
  uint16_t idx = gen(e->b);
  bool ii = e->b->type->t == TT::Int;
  if (ot->t == TT::Array) {
    Kind k = K(ot->elem);
    if (ii) emit(op3(k, OP_AGETI_I, OP_AGETI_F, OP_AGETI_R), d, obj, idx);
    else emit(op3(k, OP_AGET_I, OP_AGET_F, OP_AGET_R), d, obj, idx);
    return;
  }
  if (ot->t == TT::Bytes) {
    if (ii) {
      uint16_t t = temp(K_I);
      emit(OP_BGETI, t, obj, idx);
      emit(OP_I2F, d, t);
    } else {
      emit(OP_BGET, d, obj, idx);
    }
    return;
  }
  if (ot->t == TT::String) {
    if (ii) {
      uint16_t f = temp(K_F);
      intToNum(f, idx, e->b->type);
      idx = f;
    }
    callNative("String.index", {obj, idx}, d, 'R');
    return;
  }
  err(e->loc, "internal error: index expression");
}

// JSON.parse type descriptor:  n number  b boolean  s string  ?X nullable  [X array
//   {<classIdx>:<n>:<name>=X;...}  record/class with fields   Mk,v  Map<string, X>
std::string Gen::jsonDescriptor(Type* t) {
  switch (t->t) {
    case TT::Number: return "n";
    case TT::Bool: return "b";
    case TT::String: return "s";
    case TT::Int: return std::string("i") + (char)('0' + t->vt);
    case TT::Optional: return std::string(t->hasNull ? "?" : "") + (t->hasUndef ? "~" : "") + jsonDescriptor(t->elem);
    case TT::Array: return "[" + jsonDescriptor(t->elem);
    case TT::Tuple: {
      ClassSym* c = C.tupleRecord(t);
      std::string s = "t" + std::to_string(c->classIdx) + ":" + std::to_string(t->elems.size()) + ":";
      for (Type* x : t->elems) s += jsonDescriptor(x) + ";";
      return s;
    }
    case TT::Object: {
      std::vector<std::pair<std::string, Type*>> fs;
      for (auto& p : t->shape->props) fs.push_back({p.name, p.type});
      if (!t->shape->methods.empty()) { err(Loc(), "JSON.parse target type has methods"); return "s"; }
      Type* rt = C.recordFor(fs);
      ClassSym* c = rt->cls;
      std::string s = "{" + std::to_string(c->classIdx) + ":" + std::to_string(fs.size()) + ":";
      for (auto& f : fs) {
        for (char ch : f.first) {
          if (ch == '\\' || ch == '=' || ch == ';') s += '\\';
          s += ch;
        }
        s += "=" + jsonDescriptor(f.second) + ";";
      }
      return s;
    }
    case TT::Map:
      if (t->key->t != TT::String) { err(Loc(), "JSON.parse only supports Map<string, T>"); return "s"; }
      return "M" + jsonDescriptor(t->val);
    default:
      err(Loc(), "type '" + typeName(t) + "' cannot be produced by JSON.parse");
      return "s";
  }
}

}  // namespace

bool generateModule(Program& P, Checker& C, Module& out, const std::string& moduleName) {
  Gen g(P, C, out);
  return g.run(moduleName);
}

}  // namespace e7
