// e7vm debugger core (see debug.h).
#include "debug.h"

#include <algorithm>
#include <set>

#include "../stdlib/util.h"

namespace e7 {

namespace {

const char* vtName(uint8_t vt) {
  static const char* n[] = {"number", "boolean", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "ref"};
  return vt < VT__COUNT ? n[vt] : "?";
}

std::string normPath(const std::string& p, bool stripUp) {
  std::string s = p;
  for (char& c : s)
    if (c == '\\') c = '/';
  while (s.compare(0, 2, "./") == 0) s.erase(0, 2);
  if (stripUp)
    while (s.compare(0, 3, "../") == 0) s.erase(0, 3);
  return s;
}

bool endsWithPath(const std::string& s, const std::string& tail) {
  return s.size() > tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0 &&
         s[s.size() - tail.size() - 1] == '/';
}

// length in words of the instruction at pc (the module is verified)
size_t insLen(const uint64_t* code, size_t pc) {
  uint64_t w = code[pc];
  if (insOp(w) >= OP__COUNT) return 1;
  return insWords(w);
}

struct RawVar {
  std::string name;
  Slot v;
  uint8_t vt;
};

}  // namespace

// ------------------------------------------------------------------ module bookkeeping
bool Debugger::pathMatches(const std::string& want, const std::string& file, size_t* score) {
  std::string w = normPath(want, false), f = normPath(file, true);
  if (w.empty() || f.empty()) return false;
  size_t sc = 0;
  if (w == f || normPath(want, true) == f) sc = 1000000 + f.size();
  else if (endsWithPath(w, f)) sc = f.size();
  else if (endsWithPath(f, w)) sc = w.size();
  else return false;
  if (score) *score = sc;
  return true;
}

void Debugger::buildInfo(ModuleRT* M) {
  for (FuncRT& f : M->funcs) {
    FuncInfo fi;
    const FuncDesc& d = *f.d;
    if (d.hasDebug) {
      fi.stmts = d.debug.stmts;
    } else {
      // no debug information: every line-table entry starts a "statement"
      const auto& L = d.lines;
      for (size_t i = 0; i + 1 < L.size() && L[i] != kLineExtMarker; i += 2) {
        if (L[i] >= f.codeLen || (!fi.stmts.empty() && L[i] <= fi.stmts.back().pc)) continue;
        fi.stmts.push_back(DebugPos{L[i], L[i + 1], 0});
      }
    }
    for (auto& s : fi.stmts) fi.files.push_back(VM::sourceAt(&f, s.pc).file);
    f.dbgFlags.assign(f.codeLen, (uint8_t)DF_ALWAYS);
    for (auto& s : fi.stmts)
      if (s.pc < f.codeLen) f.dbgFlags[s.pc] |= DF_STMT;
    for (size_t pc = 0; pc < f.codeLen; pc += insLen(f.code, pc))
      if (insOp(f.code[pc]) == OP_DEBUGGER) f.dbgFlags[pc] |= DF_DEBUGGER;
    info_[&f] = std::move(fi);
  }
}

void Debugger::attached() {
  for (auto& m : vm_.modules) buildInfo(m.get());
  applyFlags();
}

void Debugger::detaching() {
  for (auto& m : vm_.modules)
    for (FuncRT& f : m->funcs) std::vector<uint8_t>().swap(f.dbgFlags);
  info_.clear();
}

void Debugger::moduleLoaded(ModuleRT* m) {
  buildInfo(m);
  applyFlags();
}

void Debugger::moduleUnloading(ModuleRT* m) {
  for (FuncRT& f : m->funcs) info_.erase(&f);
  applyFlags();  // breakpoint locations in m's functions are dropped (they resolve again on a later load)
}

void Debugger::resolve(Breakpoint& bp) {
  bp.locs.clear();
  bp.verified = false;
  bp.actualLine = 0;
  // best-matching file name first (an exact match beats a path suffix), then the first line at or
  // after the requested one that has a statement
  size_t best = 0;
  for (auto& kv : info_)
    for (auto& f : kv.second.files) {
      size_t sc;
      if (pathMatches(bp.file, f, &sc) && sc > best) best = sc;
    }
  if (!best) return;
  uint32_t line = UINT32_MAX;
  for (auto& kv : info_) {
    const FuncInfo& fi = kv.second;
    for (size_t k = 0; k < fi.stmts.size(); k++) {
      size_t sc;
      if (fi.stmts[k].line >= bp.line && fi.stmts[k].line < line && pathMatches(bp.file, fi.files[k], &sc) && sc == best)
        line = fi.stmts[k].line;
    }
  }
  if (line == UINT32_MAX) return;
  for (auto& kv : info_) {
    const FuncInfo& fi = kv.second;
    for (size_t k = 0; k < fi.stmts.size(); k++) {
      size_t sc;
      if (fi.stmts[k].line == line && pathMatches(bp.file, fi.files[k], &sc) && sc == best) {
        // first statement of the line in this function (a line with several statements stops once)
        bp.locs.push_back({kv.first, fi.stmts[k].pc});
        break;
      }
    }
  }
  bp.verified = !bp.locs.empty();
  bp.actualLine = line;
}

void Debugger::applyFlags() {
  for (auto& m : vm_.modules)
    for (FuncRT& f : m->funcs)
      for (auto& b : f.dbgFlags) b &= (uint8_t)~DF_BP;
  for (auto& bp : bps_) {
    resolve(bp);
    for (auto& l : bp.locs) {
      auto* f = const_cast<FuncRT*>(l.first);
      if (l.second < f->dbgFlags.size()) f->dbgFlags[l.second] |= DF_BP;
    }
  }
  updateMask();
}

int Debugger::addBreakpoint(const std::string& file, uint32_t line) {
  Breakpoint bp;
  bp.id = nextBpId_++;
  bp.file = file;
  bp.line = line;
  bps_.push_back(bp);
  applyFlags();
  return bp.id;
}

bool Debugger::removeBreakpoint(int id) {
  for (size_t i = 0; i < bps_.size(); i++)
    if (bps_[i].id == id) {
      bps_.erase(bps_.begin() + (std::ptrdiff_t)i);
      applyFlags();
      return true;
    }
  return false;
}

void Debugger::clearBreakpoints() {
  bps_.clear();
  applyFlags();
}

const Breakpoint* Debugger::breakpoint(int id) const {
  for (auto& b : bps_)
    if (b.id == id) return &b;
  return nullptr;
}

// ------------------------------------------------------------------ control
void Debugger::updateMask() {
  uint8_t m = DF_BP | DF_DEBUGGER;
  if (stepMode_ != DA_CONTINUE) m |= DF_STMT;
  if (skipIp_) m |= DF_ALWAYS;
  vm_.dbgMask_.store(m);
  // a pause requested concurrently must not be lost (see requestPause)
  if (pauseReq_.load()) vm_.dbgMask_.fetch_or(DF_STMT);
}

void Debugger::requestPause(int reason) {
  pauseReason_.store(reason);
  pauseReq_.store(true);
  vm_.dbgMask_.fetch_or(DF_STMT);
}

void Debugger::applyAction(int action) {
  stepMode_ = (action == DA_STEP_IN || action == DA_STEP_OVER || action == DA_STEP_OUT) ? action : (int)DA_CONTINUE;
  stepDepth_ = vm_.frames_.size();
  // a step that returns from the function stops right after the call in the caller (like V8)
  vm_.dbgRetDepth_ = stepMode_ != DA_CONTINUE ? stepDepth_ : 0;
}

int Debugger::check(const uint64_t* ip) {
  const Frame& fr = vm_.frames_.back();
  const FuncRT* fn = fr.fn;
  size_t pc = (size_t)(ip - fn->code);
  uint8_t fl = fn->dbgFlags[pc];
  size_t depth = vm_.frames_.size();
  bool always = (vm_.dbgMask_.load() & DF_ALWAYS) != 0;
  if (always) {
    // first instruction after a pause-to-host stop (it was already reported), or after a return
    // while stepping
    bool skip = skipIp_ == ip && skipDepth_ == depth;
    skipIp_ = nullptr;
    updateMask();
    if (skip) return 0;
  }
  int reason = SR_NONE;
  if (fl & DF_DEBUGGER) reason = SR_DEBUGGER;
  else if (fl & DF_BP) reason = SR_BREAKPOINT;
  else if ((fl & DF_STMT) && pauseReq_.load()) reason = pauseReason_.load();
  else if (stepMode_ != DA_CONTINUE && depth < stepDepth_) reason = SR_STEP;  // returned to a caller
  else if ((fl & DF_STMT) && (stepMode_ == DA_STEP_IN || (stepMode_ == DA_STEP_OVER && depth <= stepDepth_)))
    reason = SR_STEP;
  if (reason == SR_NONE) return 0;
  // pause-to-host cannot unwind the C++ frames of a native that called back into script
  if (!onStop && vm_.nativeDepth_ > 0) return 0;
  hits_.clear();
  if (reason == SR_BREAKPOINT)
    for (auto& bp : bps_)
      for (auto& l : bp.locs)
        if (l.first == fn && l.second == pc) hits_.push_back(bp.id);
  return stop(reason, ip, true);
}

int Debugger::stop(int reason, const uint64_t* ip, bool exact) {
  paused_ = true;
  reason_ = reason;
  exact_ = exact;
  if (reason != SR_EXCEPTION) message_.clear();
  pauseReq_.store(false);
  stepMode_ = DA_CONTINUE;
  vm_.dbgRetDepth_ = 0;
  clearRefs();
  if (onStop) {
    inHandler_ = true;
    int action;
    try {
      action = onStop(reason);
    } catch (...) {
      inHandler_ = false;
      paused_ = false;
      clearRefs();
      throw;
    }
    inHandler_ = false;
    paused_ = false;
    clearRefs();
    if (reason == SR_EXCEPTION) {  // the error terminates the script whatever the handler said
      updateMask();
      return 0;
    }
    if (action == DA_ABORT) {
      aborting_ = true;
      updateMask();
      vm_.errorCode(ERR_RUNTIME, "execution aborted by the debugger");
    }
    applyAction(action);
    updateMask();
    return 0;
  }
  hostPaused_ = true;
  skipIp_ = ip;
  skipDepth_ = vm_.frames_.size();
  updateMask();
  return ERR_PAUSED;
}

void Debugger::prepareResume(int action) {
  if (!hostPaused()) return;
  paused_ = hostPaused_ = false;
  clearRefs();
  applyAction(action);
  updateMask();
}

void Debugger::errorRaised(int code, const std::string& msg, const std::string& full, bool caught) {
  // a try/catch handler takes it: a stop only when asked for, and here only for the synchronous handler
  // (pause-to-host stops for caught errors happen in VM::catchError, where the C++ frames are gone)
  if (caught && (!breakOnCaught || !onStop)) return;
  if (!breakOnError || inHandler_ || aborting_ || paused_ || vm_.catchDepth_ > 0 || vm_.frames_.empty()) return;
  if (code != ERR_RUNTIME && code != ERR_BUDGET && code != ERR_MEMORY && code != ERR_STACK) return;
  message_ = msg;
  hits_.clear();
  if (onStop) {
    stop(SR_EXCEPTION, nullptr, false);
    return;
  }
  // pause-to-host: the VM keeps the frames when the error reaches runFrames()
  paused_ = hostPaused_ = true;
  reason_ = SR_EXCEPTION;
  exact_ = false;
  stepMode_ = DA_CONTINUE;
  vm_.dbgRetDepth_ = 0;
  clearRefs();
  errorPending_ = true;
  errCode_ = code;
  errFull_ = full;
}

bool Debugger::takeErrorPause() { return errorPending_ && hostPaused_; }

bool Debugger::wantsCaughtHostStop() const {
  return breakOnCaught && !onStop && !inHandler_ && !aborting_ && !paused_ && vm_.catchDepth_ == 0;
}

void Debugger::caughtHostStop(const std::string& msg) {
  message_ = msg;
  hits_.clear();
  paused_ = hostPaused_ = true;
  reason_ = SR_EXCEPTION;
  exact_ = false;
  stepMode_ = DA_CONTINUE;
  vm_.dbgRetDepth_ = 0;
  skipIp_ = nullptr;
  clearRefs();
  updateMask();
}

void Debugger::reset() {
  paused_ = hostPaused_ = inHandler_ = aborting_ = false;
  errorPending_ = false;
  skipIp_ = nullptr;
  stepMode_ = DA_CONTINUE;
  vm_.dbgRetDepth_ = 0;
  clearRefs();
  updateMask();
}

// ------------------------------------------------------------------ inspection
void Debugger::clearRefs() {
  refs_.clear();
  objRefs_.clear();
}

size_t Debugger::frameCount() const { return paused_ ? vm_.frames_.size() : 0; }

const Frame* Debugger::frameAt(size_t i) const {
  if (!paused_ || i >= vm_.frames_.size()) return nullptr;
  return &vm_.frames_[vm_.frames_.size() - 1 - i];
}

ptrdiff_t Debugger::pcOf(size_t i) const {
  const Frame* f = frameAt(i);
  if (!f || !f->ip) return 0;
  ptrdiff_t pc = f->ip - f->fn->code;
  if (!(i == 0 && exact_)) pc--;  // other frames: inside the call instruction
  if (pc < 0) pc = 0;
  if ((size_t)pc >= f->fn->codeLen) pc = (ptrdiff_t)f->fn->codeLen - 1;
  return pc;
}

DebugFrame Debugger::frame(size_t i) const {
  DebugFrame r;
  const Frame* f = frameAt(i);
  if (!f) return r;
  r.function = f->fn->name;
  ptrdiff_t pc = pcOf(i);
  r.loc = VM::sourceAt(f->fn, pc);
  if (i == 0 && exact_) {
    // stopped at a statement boundary: report the statement's own position
    auto it = info_.find(f->fn);
    if (it != info_.end()) {
      const auto& st = it->second.stmts;
      auto s = std::lower_bound(st.begin(), st.end(), (uint32_t)pc, [](const DebugPos& p, uint32_t x) { return p.pc < x; });
      if (s != st.end() && s->pc == (uint32_t)pc && s->line) {
        r.loc.line = s->line;
        r.loc.col = s->col;
      }
    }
  }
  return r;
}

uint32_t Debugger::refFor(Obj* o) {
  auto it = objRefs_.find(o);
  if (it != objRefs_.end()) return it->second;
  refs_.push_back(Ref{3, 0, o});
  uint32_t id = (uint32_t)refs_.size();
  objRefs_[o] = id;
  return id;
}

DebugVar Debugger::makeVar(const std::string& name, Slot v, uint8_t vt) {
  DebugVar r;
  r.name = name;
  r.value = inspectValue(vm_, v, vt, 2, false);
  if (r.value.size() > 400) r.value = r.value.substr(0, 397) + "...";
  if (vt != VT_REF) {
    r.type = vtName(vt);
    return r;
  }
  Obj* o = v.r;
  if (!o) { r.type = "null"; return r; }
  if (o == vm_.undef()) { r.type = "undefined"; return r; }
  switch (o->type) {
    case OT::String: r.type = "string"; break;
    case OT::Array: r.type = "Array"; r.count = static_cast<ArrObj*>(o)->len; break;
    case OT::Bytes: r.type = "Uint8Array"; r.count = static_cast<BytesObj*>(o)->len; break;
    case OT::Buffer: r.type = "ArrayBuffer"; break;
    case OT::Instance: {
      ClassRT* c = static_cast<InstObj*>(o)->cls;
      r.type = c->isTuple ? "tuple" : c->isRecord ? "Object" : c->name;
      r.count = c->nfields;
      break;
    }
    case OT::Closure: r.type = "function"; r.count = static_cast<CloObj*>(o)->nup; break;
    case OT::Map: {
      auto* m = static_cast<MapObj*>(o);
      r.type = m->isSet ? "Set" : "Map";
      r.count = m->count;
      break;
    }
    case OT::Box: {
      auto* b = static_cast<BoxObj*>(o);
      r.type = (b->flags & 1) ? "Date" : vtName(b->vt);
      break;
    }
    case OT::BigInt: r.type = "bigint"; break;
    case OT::Handle: r.type = "KeyHandle"; break;
    default: r.type = "object"; break;
  }
  if (r.count) r.ref = refFor(o);
  return r;
}

static std::vector<RawVar> rawScope(VM& vm, const Frame* f, ptrdiff_t pc, int kind, Obj* undef) {
  std::vector<RawVar> out;
  if (!f) return out;
  const FuncRT* fn = f->fn;
  const FuncDesc* d = fn->d;
  if (kind == 0) {
    if (!d->hasDebug) return out;
    Slot* R = vm.stackBase() + f->base;
    // live variables; with shadowing, the innermost (latest declared) one wins
    std::vector<const DebugLocal*> live;
    for (auto& l : d->debug.locals)
      if ((ptrdiff_t)l.startPc <= pc && pc < (ptrdiff_t)l.endPc && l.reg < fn->nregs) live.push_back(&l);
    std::stable_sort(live.begin(), live.end(), [](const DebugLocal* a, const DebugLocal* b) {
      bool pa = a->flags & kDebugLocalParam, pb = b->flags & kDebugLocalParam;
      if (pa != pb) return pa;
      if (pa) return a->reg < b->reg;
      return a->startPc < b->startPc;
    });
    std::set<std::string> seen;
    std::vector<const DebugLocal*> keep;
    for (size_t k = live.size(); k-- > 0;)
      if (seen.insert(live[k]->name).second) keep.push_back(live[k]);
    std::reverse(keep.begin(), keep.end());
    for (const DebugLocal* l : keep) {
      Slot s = R[l->reg];
      uint8_t vt = l->vt;
      if (l->flags & kDebugLocalCell) {
        Obj* o = s.r;
        if (o && o->type == OT::Cell) {
          s = static_cast<CellObj*>(o)->v;
          vt = static_cast<CellObj*>(o)->vt;
        } else {
          s.r = undef;
          vt = VT_REF;
        }
      }
      out.push_back(RawVar{l->name, s, vt});
    }
  } else if (kind == 1) {
    CloObj* c = f->closure;
    if (!c) return out;
    const auto& names = d->debug.upvalNames;
    for (uint32_t k = 0; k < c->nup; k++) {
      CellObj* cell = c->ups()[k];
      if (!cell || cell->type != OT::Cell) continue;
      std::string nm = d->hasDebug && k < names.size() ? names[k] : "$up" + std::to_string(k);
      out.push_back(RawVar{nm, cell->v, cell->vt});
    }
  } else {
    ModuleRT* M = fn->mod;
    for (size_t k = 0; k < M->globals.size() && k < M->m.globals.size(); k++) {
      const GlobalDesc& g = M->m.globals[k];
      if (g.name.empty() || g.name[0] == '<') continue;
      out.push_back(RawVar{g.name, M->globals[k], g.vt});
    }
  }
  return out;
}

std::vector<DebugScope> Debugger::scopes(size_t frame) {
  std::vector<DebugScope> out;
  const Frame* f = frameAt(frame);
  if (!f) return out;
  static const char* names[] = {"Locals", "Closure", "Globals"};
  for (int k = 0; k < 3; k++) {
    size_t n = rawScope(vm_, f, pcOf(frame), k, vm_.undef()).size();
    if (k == 1 && n == 0) continue;
    if (k == 2 && n == 0) continue;
    refs_.push_back(Ref{k, frame, nullptr});
    out.push_back(DebugScope{names[k], (uint32_t)refs_.size(), n});
  }
  return out;
}

std::vector<DebugVar> Debugger::scopeVars(const Ref& r) {
  std::vector<DebugVar> out;
  for (auto& rv : rawScope(vm_, frameAt(r.frame), pcOf(r.frame), r.kind, vm_.undef())) out.push_back(makeVar(rv.name, rv.v, rv.vt));
  return out;
}

std::vector<DebugVar> Debugger::objectVars(Obj* o, size_t start, size_t count) {
  std::vector<DebugVar> out;
  size_t end = count ? start + count : SIZE_MAX;
  if (end < start) end = SIZE_MAX;
  switch (o->type) {
    case OT::Array: {
      auto* a = static_cast<ArrObj*>(o);
      for (size_t i = start; i < a->len && i < end; i++) out.push_back(makeVar(std::to_string(i), a->data[i], a->vt));
      break;
    }
    case OT::Bytes: {
      auto* b = static_cast<BytesObj*>(o);
      for (size_t i = start; i < b->len && i < end; i++) {
        Slot s;
        s.i = b->ptr()[i];
        out.push_back(makeVar(std::to_string(i), s, VT_U8));
      }
      break;
    }
    case OT::Instance: {
      auto* in = static_cast<InstObj*>(o);
      ClassRT* c = in->cls;
      for (size_t i = start; i < c->nfields && i < end; i++)
        out.push_back(makeVar(c->fieldNames[i], in->fields()[i], c->fieldVT[i]));
      break;
    }
    case OT::Map: {
      auto* m = static_cast<MapObj*>(o);
      size_t idx = 0;
      for (auto& e : m->entries) {
        if (!e.live) continue;
        if (idx >= start && idx < end) {
          if (m->isSet) out.push_back(makeVar("[" + std::to_string(idx) + "]", e.k, m->kvt));
          else out.push_back(makeVar(inspectValue(vm_, e.k, m->kvt, 3, false), e.v, m->vvt));
        }
        idx++;
      }
      break;
    }
    case OT::Closure: {
      auto* c = static_cast<CloObj*>(o);
      const FuncDesc* d = c->fn->d;
      for (size_t k = start; k < c->nup && k < end; k++) {
        CellObj* cell = c->ups()[k];
        if (!cell || cell->type != OT::Cell) continue;
        std::string nm = d->hasDebug && k < d->debug.upvalNames.size() ? d->debug.upvalNames[k] : "$up" + std::to_string(k);
        out.push_back(makeVar(nm, cell->v, cell->vt));
      }
      break;
    }
    default: break;
  }
  return out;
}

std::vector<DebugVar> Debugger::variables(uint32_t ref, size_t start, size_t count) {
  if (!paused_ || ref == 0 || ref > refs_.size()) return {};
  Ref r = refs_[ref - 1];
  if (r.kind == 3) return objectVars(r.obj, start, count);
  std::vector<DebugVar> all = scopeVars(r);
  if (start >= all.size()) return {};
  size_t end = count && start + count < all.size() ? start + count : all.size();
  return std::vector<DebugVar>(all.begin() + (std::ptrdiff_t)start, all.begin() + (std::ptrdiff_t)end);
}

// one access step of evaluate(): field / index / map key / length
static bool member(VM& vm, Slot v, uint8_t vt, const std::string& key, bool bracket, Slot& out, uint8_t& ovt) {
  if (vt != VT_REF || !v.r || v.r == vm.undef()) return false;
  Obj* o = v.r;
  bool isIndex = !key.empty() && key.size() < 10 && std::all_of(key.begin(), key.end(), [](char c) { return c >= '0' && c <= '9'; });
  uint32_t idx = isIndex ? (uint32_t)std::stoul(key) : 0;
  switch (o->type) {
    case OT::String:
      if (key == "length" && !bracket) { out.f = static_cast<StrObj*>(o)->len; ovt = VT_NUMBER; return true; }
      return false;
    case OT::Array: {
      auto* a = static_cast<ArrObj*>(o);
      if (key == "length" && !bracket) { out.f = a->len; ovt = VT_NUMBER; return true; }
      if (!isIndex || idx >= a->len) return false;
      out = a->data[idx];
      ovt = a->vt;
      return true;
    }
    case OT::Bytes: {
      auto* b = static_cast<BytesObj*>(o);
      if (key == "length" && !bracket) { out.f = b->len; ovt = VT_NUMBER; return true; }
      if (!isIndex || idx >= b->len) return false;
      out.i = b->ptr()[idx];
      ovt = VT_U8;
      return true;
    }
    case OT::Instance: {
      auto* in = static_cast<InstObj*>(o);
      auto it = in->cls->fieldIndex.find(key);
      if (it == in->cls->fieldIndex.end() || it->second >= in->cls->nfields) {
        if (in->cls->isTuple && key == "length" && !bracket) { out.f = in->cls->nfields; ovt = VT_NUMBER; return true; }
        return false;
      }
      out = in->fields()[it->second];
      ovt = in->cls->fieldVT[it->second];
      return true;
    }
    case OT::Map: {
      auto* m = static_cast<MapObj*>(o);
      if (key == "size" && !bracket) { out.f = m->count; ovt = VT_NUMBER; return true; }
      if (!bracket) return false;
      for (auto& e : m->entries)
        if (e.live && inspectValue(vm, e.k, m->kvt, 3, true) == key) {
          out = m->isSet ? e.k : e.v;
          ovt = m->isSet ? m->kvt : m->vvt;
          return true;
        }
      return false;
    }
    case OT::Box: {
      auto* b = static_cast<BoxObj*>(o);
      return member(vm, b->v, b->vt, key, bracket, out, ovt);
    }
    default: return false;
  }
}

bool Debugger::evaluate(size_t frame, const std::string& expr, DebugVar& out, std::string& err) {
  const Frame* f = frameAt(frame);
  if (!f) { err = "not paused"; return false; }
  size_t i = 0, n = expr.size();
  auto ws = [&] { while (i < n && (expr[i] == ' ' || expr[i] == '\t')) i++; };
  auto identChar = [](char c, bool first) {
    return c == '_' || c == '$' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (!first && c >= '0' && c <= '9') || (unsigned char)c >= 0x80;
  };
  ws();
  size_t s0 = i;
  while (i < n && identChar(expr[i], i == s0)) i++;
  std::string root = expr.substr(s0, i - s0);
  if (root.empty()) { err = "expected a variable name"; return false; }
  Slot v;
  uint8_t vt = VT_REF;
  bool found = false;
  for (int k = 0; k < 3 && !found; k++)
    for (auto& rv : rawScope(vm_, f, pcOf(frame), k, vm_.undef()))
      if (rv.name == root) { v = rv.v; vt = rv.vt; found = true; break; }
  if (!found) { err = "no variable named '" + root + "'"; return false; }
  for (;;) {
    ws();
    if (i >= n) break;
    std::string key;
    bool bracket = false;
    if (expr[i] == '.') {
      i++;
      ws();
      size_t s = i;
      while (i < n && identChar(expr[i], i == s)) i++;
      key = expr.substr(s, i - s);
      if (key.empty()) { err = "expected a property name"; return false; }
    } else if (expr[i] == '[') {
      i++;
      ws();
      bracket = true;
      if (i < n && (expr[i] == '\'' || expr[i] == '"')) {
        char q = expr[i++];
        size_t s = i;
        while (i < n && expr[i] != q) i++;
        if (i >= n) { err = "unterminated string"; return false; }
        key = expr.substr(s, i - s);
        i++;
      } else {
        size_t s = i;
        while (i < n && expr[i] != ']') i++;
        key = expr.substr(s, i - s);
        while (!key.empty() && key.back() == ' ') key.pop_back();
      }
      ws();
      if (i >= n || expr[i] != ']') { err = "expected ']'"; return false; }
      i++;
    } else {
      err = "unexpected '" + std::string(1, expr[i]) + "'";
      return false;
    }
    Slot nv;
    uint8_t nvt;
    if (!member(vm_, v, vt, key, bracket, nv, nvt)) { err = "cannot read '" + key + "'"; return false; }
    v = nv;
    vt = nvt;
  }
  out = makeVar(expr, v, vt);
  return true;
}

}  // namespace e7
