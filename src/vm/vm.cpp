#include "vm.h"

#include "debug.h"

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace e7 {

// ------------------------------------------------------------------ strings
uint32_t strHash(StrObj* s) {
  if (s->hash) return s->hash;
  uint32_t h = 2166136261u;
  const char16_t* c = s->chars();
  for (uint32_t i = 0; i < s->len; i++) { h ^= c[i]; h *= 16777619u; }
  if (h == 0) h = 1;
  s->hash = h;
  return h;
}
bool strEq(const StrObj* a, const StrObj* b) {
  if (a == b) return true;
  if (!a || !b || a->len != b->len) return false;
  if (a->hash && b->hash && a->hash != b->hash) return false;
  return memcmp(a->chars(), b->chars(), a->len * 2) == 0;
}
int strCmp(const StrObj* a, const StrObj* b) {
  uint32_t n = a->len < b->len ? a->len : b->len;
  const char16_t* x = a->chars();
  const char16_t* y = b->chars();
  for (uint32_t i = 0; i < n; i++)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return a->len < b->len ? -1 : a->len > b->len ? 1 : 0;
}
std::u16string strU16(const StrObj* s) { return std::u16string(s->chars(), s->len); }
std::string strUtf8(const StrObj* s) { return utf16ToUtf8(s->chars(), s->len); }

// ------------------------------------------------------------------ VM
VM::VM(const VMConfig& cfg) : cfg_(cfg) {
  if (cfg_.stackSlots < 1024) cfg_.stackSlots = 1024;
  if (cfg_.maxFrames < 16) cfg_.maxFrames = 16;
  // frames_ must never reallocate while executing (the interpreter holds pointers into it), so its
  // capacity is reserved up front; clamp absurd values ("unlimited" sentinels) to a sane maximum.
  if (cfg_.maxFrames > 200000) cfg_.maxFrames = 200000;
  if (cfg_.stackSlots > (1u << 22)) cfg_.stackSlots = 1u << 22;  // 32 MiB of registers
  if (cfg_.memoryLimit < (1u << 20)) cfg_.memoryLimit = 1u << 20;
  frames_.reserve((size_t)cfg_.maxFrames + 1);
  undef_ = static_cast<Obj*>(allocRaw(OT::Undefined, sizeof(Obj)));
  pin(undef_);
  stack_ = static_cast<Slot*>(calloc(cfg_.stackSlots, sizeof(Slot)));
  if (!stack_) {
    free(undef_);  // the destructor does not run when a constructor throws
    throw std::bad_alloc();
  }
  registerCoreNatives(*this);
  registerStringNatives(*this);
  registerArrayNatives(*this);
  registerMapNatives(*this);
  registerMathNatives(*this);
  registerBytesNatives(*this);
  registerBigIntNatives(*this);
  registerJsonNatives(*this);
  registerCryptoNatives(*this);
  registerDateNatives(*this);
  registerTestNatives(*this);
  applyNativeCosts(*this);
}

VM::~VM() {
  Obj* o = objects_;
  while (o) {
    Obj* n = o->gcNext;
    freeObj(o);
    o = n;
  }
  free(stack_);
}

static uint64_t objUnits(const Obj* o, uint64_t& limbs) {
  if (!o) return 0;
  switch (o->type) {
    case OT::String: return static_cast<const StrObj*>(o)->len;
    case OT::Bytes: return static_cast<const BytesObj*>(o)->len;
    case OT::Array: return (uint64_t)static_cast<const ArrObj*>(o)->len * 4;
    case OT::Map: return (uint64_t)static_cast<const MapObj*>(o)->count * 8;
    case OT::BigInt: {
      uint64_t n = static_cast<const BigObj*>(o)->mag.size();
      limbs = std::max(limbs, n);
      return n * 4;
    }
    default: return 0;
  }
}
uint64_t VM::nativeCost(const NativeBinding& nb, const Slot* args, uint32_t argc, Slot ret) const {
  uint64_t in = 0, maxLimbs = 0, secondLimbs = 0;
  for (uint32_t k = 0; k < argc && k < 64; k++) {
    if (!(nb.refArgs >> k & 1)) continue;
    uint64_t l = 0;
    in += objUnits(args[k].r, l);
    if (l > maxLimbs) { secondLimbs = maxLimbs; maxLimbs = l; }
    else if (l > secondLimbs) secondLimbs = l;
  }
  uint64_t outLimbs = 0;
  uint64_t out = nb.retKind == 'R' ? objUnits(ret.r, outLimbs) : 0;
  switch (nb.cost) {
    case NC_SCAN: return (in + out) / 8;
    case NC_OUT: return out / 8;
    case NC_HASH: return in + out;
    case NC_SORT: {
      uint64_t n = in / 4, lg = 1;
      while ((1ull << lg) < n) lg++;
      return n * lg * 4;
    }
    case NC_BIGQ: {
      uint64_t a = std::max(maxLimbs, outLimbs), b = secondLimbs ? secondLimbs : a;
      return a * b / 4 + (in + out) / 8;
    }
    default: return 0;
  }
}

void VM::registerNative(const std::string& name, const std::string& sig, NativeFn fn, void* ud) {
  natives_[name] = NativeEntry{sig, fn, ud, NC_NONE};
}
const NativeEntry* VM::findNative(const std::string& name) const {
  auto it = natives_.find(name);
  return it == natives_.end() ? nullptr : &it->second;
}

uint32_t VM::sigId(const std::string& sig) {
  auto it = sigIds_.find(sig);
  if (it != sigIds_.end()) return it->second;
  uint32_t id = (uint32_t)sigs_.size();
  sigs_.push_back(sig);
  sigIds_[sig] = id;
  return id;
}

void VM::print(const std::string& s) {
  if (printFn) printFn(printUd, s.data(), s.size());
  else { fwrite(s.data(), 1, s.size(), stdout); fputc('\n', stdout); }
}

void VM::seedRandom(uint64_t seed) {
  uint64_t z = seed + 0x9E3779B97F4A7C15ull;
  for (int i = 0; i < 2; i++) {
    z += 0x9E3779B97F4A7C15ull;
    uint64_t x = z;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    rng_[i] = x ^ (x >> 31);
  }
  if (!rng_[0] && !rng_[1]) rng_[0] = 1;
}
double VM::randomDouble() {
  uint64_t s1 = rng_[0];
  const uint64_t s0 = rng_[1];
  rng_[0] = s0;
  s1 ^= s1 << 23;
  rng_[1] = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26);
  uint64_t r = rng_[1] + s0;
  return (double)(r >> 11) * (1.0 / 9007199254740992.0);
}

// ------------------------------------------------------------------ errors
// Errors that Node.js throws as catchable exceptions carry the JavaScript error name as a message
// prefix ("RangeError: invalid count value: -1"); see docs/SPEC.md (try/catch).
static bool jsErrorName(const std::string& msg, std::string& name, std::string& text) {
  static const char* const kNames[] = {"Error", "TypeError", "RangeError", "SyntaxError", "URIError"};
  for (const char* n : kNames) {
    size_t l = strlen(n);
    if (msg.size() > l + 1 && msg.compare(0, l, n) == 0 && msg[l] == ':' && msg[l + 1] == ' ') {
      name = n;
      text = msg.substr(l + 2);
      return true;
    }
  }
  return false;
}

void VM::raise(int code, const std::string& msg, bool allowCatch) {
  E7Error e(code, msg);
  if (allowCatch && code == ERR_RUNTIME) jsErrorName(msg, e.jsName, e.text);
  // a handler will take it: skip the stack trace (O(frames), and uncharged), it is never shown
  if (!e.jsName.empty() && willBeCaught()) {
    if (E7_UNLIKELY(dbg_ != nullptr) && !frames_.empty()) dbg_->errorRaised(code, msg, msg, true);
    throw e;
  }
  std::string tr = stackTrace();
  if (!tr.empty()) e.msg += "\n" + tr;
  if (E7_UNLIKELY(dbg_ != nullptr) && !frames_.empty()) dbg_->errorRaised(code, msg, e.msg, false);
  throw e;
}

void VM::raiseThrown(Obj* error) {
  StrObj *n, *m;
  if (!errorParts(error, &n, &m)) raise(ERR_RUNTIME, "throw: not an Error object", false);
  std::string name = strUtf8(n), text = strUtf8(m);
  // uncaught: "message" for Error (as before 0.2), "TypeError: message" for the others
  std::string msg = name == "Error" ? text : name + ": " + text;
  E7Error e(ERR_RUNTIME, msg);
  e.jsName = name.empty() ? "Error" : name;
  e.text = text;
  e.value = error;
  if (willBeCaught()) {  // no stack trace for a caught error (see raise)
    if (E7_UNLIKELY(dbg_ != nullptr) && !frames_.empty()) dbg_->errorRaised(ERR_RUNTIME, msg, msg, true);
    throw e;
  }
  std::string tr = stackTrace();
  if (!tr.empty()) e.msg += "\n" + tr;
  if (E7_UNLIKELY(dbg_ != nullptr) && !frames_.empty()) dbg_->errorRaised(ERR_RUNTIME, msg, e.msg, false);
  throw e;
}

ArrObj* VM::newErrorObject(const std::string& name, const std::string& message) {
  StrObj* n = newStringUtf8(name);
  TempRoot rn(*this, n);
  StrObj* m = newStringUtf8(message);
  TempRoot rm(*this, m);
  ArrObj* a = newArray(VT_REF, 2);
  a->data[0].r = n;
  a->data[1].r = m;
  a->len = 2;
  a->flags |= kArrFlagError;
  return a;
}

bool VM::errorParts(Obj* o, StrObj** name, StrObj** message) const {
  if (!o || o->type != OT::Array) return false;
  auto* a = static_cast<ArrObj*>(o);
  if (!(a->flags & kArrFlagError) || a->vt != VT_REF || a->len != 2) return false;
  Obj* n = a->data[0].r;
  Obj* m = a->data[1].r;
  if (!n || n->type != OT::String || !m || m->type != OT::String) return false;
  *name = static_cast<StrObj*>(n);
  *message = static_cast<StrObj*>(m);
  return true;
}

const HandlerDesc* VM::handlerAt(size_t k) const {
  const Frame& f = frames_[k];
  const FuncDesc* d = f.fn->d;
  if (!d || d->handlers.empty() || !f.ip) return nullptr;
  // the frame's ip is past the current instruction (the raising instruction, or the call that is
  // running in the frames above); ip - 1 lies inside it
  size_t pc = (size_t)(f.ip - f.fn->code);
  if (pc == 0) return nullptr;
  pc--;
  for (const HandlerDesc& h : d->handlers)  // innermost first
    if (pc >= h.start && pc < h.end) return &h;
  return nullptr;
}

bool VM::willBeCaught() const {
  if (catchDepth_ > 0) return true;
  for (size_t k = frames_.size(); k-- > 0;)
    if (handlerAt(k)) return true;
  return false;
}

// try/catch: called by execute() for a catchable error. Finds the innermost frame of this
// execute() call (frames >= entryDepth) whose handler covers its current instruction, drops the
// frames above it, restores the native-call state to what it was while that frame ran, stores the
// Error object in the handler's register and continues at the handler.
int VM::catchError(E7Error& e, size_t entryDepth, const ExecState& st) {
  for (size_t k = frames_.size(); k-- > entryDepth;) {
    const HandlerDesc* h = handlerAt(k);
    if (!h) continue;
    // a debugger in pause-to-host mode that stops on caught errors: stop before the handler runs,
    // with the frames of the throw still in place; resume() completes the catch (only from the
    // top level: the C++ frames of a native cannot be kept for a pause)
    if (E7_UNLIKELY(dbg_ != nullptr) && st.nativeDepth == 0 && dbg_->wantsCaughtHostStop()) {
      nativeDepth_ = st.nativeDepth;
      curNative_ = st.curNative;
      callbackNatives_.resize(st.callbacks);
      tempRoots_.resize(st.tempRoots);
      gcInhibit_ = st.gcInhibit;
      Obj* v = e.value;
      if (!v) v = newErrorObject(e.jsName, e.text);
      stack_[frames_[k].base + h->reg].r = v;  // a GC root while paused
      pendingCatch_ = PendingCatch{true, k, h};
      dbg_->caughtHostStop(e.text.empty() ? e.jsName : e.jsName + ": " + e.text);
      return 2;
    }
    // unwinding is charged like the calls it undoes (one tick per dropped frame, one for the catch);
    // an exhausted budget is reported at the next safepoint in the handler's code
    budget_ -= (int64_t)(frames_.size() - k);
    frames_.resize(k + 1);
    nativeDepth_ = st.nativeDepth;
    curNative_ = st.curNative;
    callbackNatives_.resize(st.callbacks);
    tempRoots_.resize(st.tempRoots);
    gcInhibit_ = st.gcInhibit;
    Frame& f = frames_[k];
    Obj* v = e.value;
    if (!v) v = newErrorObject(e.jsName, e.text);  // may collect: frames and registers are consistent
    Frame& g = frames_[k];
    stack_[g.base + h->reg].r = v;
    g.ip = g.fn->code + h->handler;
    (void)f;
    return 1;
  }
  return 0;
}

// Completes a catch that a pause-to-host debugger stop interrupted (see catchError).
void VM::finishPendingCatch() {
  if (!pendingCatch_.active) return;
  PendingCatch pc = pendingCatch_;
  pendingCatch_ = PendingCatch{};
  budget_ -= (int64_t)(frames_.size() - pc.frame);
  frames_.resize(pc.frame + 1);
  Frame& g = frames_[pc.frame];
  g.ip = g.fn->code + pc.handler->handler;  // the Error object is already in the handler's register
}
void VM::error(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  raise(ERR_RUNTIME, buf);
}
void VM::errorCode(int code, const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  raise(code, buf);
}

// Source position of instruction `pc` of function f: file name (the module name unless the
// line-table extension names another source file), line, and column (debug information only).
SrcLoc VM::sourceAt(const FuncRT* f, ptrdiff_t pc) {
  SrcLoc r;
  r.file = f && f->mod ? f->mod->m.name : std::string();
  if (!f || !f->d) return r;
  if (pc < 0) pc = 0;
  const auto& L = f->d->lines;
  size_t i = 0;
  for (; i + 1 < L.size(); i += 2) {
    if (L[i] == kLineExtMarker) break;
    if ((ptrdiff_t)L[i] > pc) {
      while (i + 1 < L.size() && L[i] != kLineExtMarker) i += 2;
      break;
    }
    r.line = L[i + 1];
  }
  if (i + 1 < L.size() && L[i] == kLineExtMarker && L[i + 1] == kLineExtFiles) {
    const auto& strs = f->mod->m.strs;
    for (size_t j = i + 2; j + 3 < L.size(); j += 4) {
      if (L[j] != kLineExtMarker || L[j + 2] != kLineExtMarker) break;
      if ((ptrdiff_t)L[j + 1] > pc) break;
      if (L[j + 3] < strs.size()) r.file = utf16ToUtf8(strs[L[j + 3]]);
    }
  }
  if (f->d->hasDebug && !f->d->debug.pos.empty()) {
    // debug position table: the last entry at or before pc gives line and column
    const auto& P = f->d->debug.pos;
    size_t lo = 0, hi = P.size();
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      if ((ptrdiff_t)P[mid].pc <= pc) lo = mid + 1;
      else hi = mid;
    }
    if (lo > 0) {
      r.line = P[lo - 1].line;
      r.col = P[lo - 1].col;
    }
  }
  return r;
}

std::string VM::stackTrace() const {
  // one entry per frame, innermost first; natives that called back into script appear between
  // the callback frame and the frame that called the native
  struct Entry { std::string name, where; };
  std::vector<Entry> es;
  // (e7:test assertion natives are left out: their message already says what failed)
  if (curNative_ && curNative_->name.rfind("test.", 0) != 0) es.push_back(Entry{curNative_->name, "native"});
  size_t cb = callbackNatives_.size();
  for (size_t i = frames_.size(); i-- > 0;) {
    const Frame& fr = frames_[i];
    // fr.ip is past the instruction that raised the error or made the call
    SrcLoc loc = sourceAt(fr.fn, fr.ip ? fr.ip - fr.fn->code - 1 : 0);
    std::string where = loc.file + ":" + std::to_string(loc.line);
    if (loc.col) where += ":" + std::to_string(loc.col);
    es.push_back(Entry{fr.fn->name, where});
    while (cb > 0 && callbackNatives_[cb - 1].first == i) {
      cb--;
      if (callbackNatives_[cb].second) es.push_back(Entry{callbackNatives_[cb].second->name, "native"});
    }
  }
  // collapse repeated cycles (recursion): print one cycle, then a summary line
  std::vector<std::string> lines;
  auto same = [&](size_t a, size_t b) { return es[a].name == es[b].name && es[a].where == es[b].where; };
  for (size_t i = 0; i < es.size();) {
    size_t bestP = 0, bestReps = 1;
    for (size_t p = 1; p <= 8 && i + 2 * p <= es.size(); p++) {
      size_t reps = 1;
      while (i + (reps + 1) * p <= es.size()) {
        bool eq = true;
        for (size_t k = 0; k < p && eq; k++) eq = same(i + k, i + reps * p + k);
        if (!eq) break;
        reps++;
      }
      if (reps >= 3 && reps * p > bestReps * (bestP ? bestP : 1)) { bestP = p; bestReps = reps; }
    }
    if (!bestP) {
      lines.push_back("    at " + es[i].name + " (" + es[i].where + ")");
      i++;
      continue;
    }
    for (size_t k = 0; k < bestP; k++) lines.push_back("    at " + es[i + k].name + " (" + es[i + k].where + ")");
    size_t more = (bestReps - 1) * bestP;
    std::string names;
    for (size_t k = 0; k < bestP; k++) {
      bool dup = false;
      for (size_t q = 0; q < k; q++) dup = dup || es[i + q].name == es[i + k].name;
      if (!dup) names += (names.empty() ? "" : ", ") + es[i + k].name;
    }
    lines.push_back("    ... " + std::to_string(more) + " more frame" + (more == 1 ? "" : "s") + " of " + names + " (recursion)");
    i += bestReps * bestP;
  }
  // very deep (non-repetitive) stacks: keep the innermost and outermost frames
  const size_t kHead = 20, kTail = 5;
  if (lines.size() > kHead + kTail + 1) {
    size_t omitted = lines.size() - kHead - kTail;
    std::vector<std::string> cut(lines.begin(), lines.begin() + kHead);
    cut.push_back("    ... " + std::to_string(omitted) + " more lines");
    cut.insert(cut.end(), lines.end() - kTail, lines.end());
    lines.swap(cut);
  }
  std::string out;
  for (auto& l : lines) out += (out.empty() ? "" : "\n") + l;
  return out;
}

// ------------------------------------------------------------------ heap
void* VM::allocRaw(OT t, size_t size) {
  if (E7_UNLIKELY(bytes_ + size > cfg_.memoryLimit)) {
    // Emergency collection. Safe when no native code is running: every live reference is then
    // held in a register, a global, a pinned object or the object graph.
    if (nativeDepth_ == 0 && gcInhibit_ == 0) collectGarbage();
    if (bytes_ + size > cfg_.memoryLimit) errorCode(ERR_MEMORY, "out of memory (limit %zu bytes)", cfg_.memoryLimit);
  }
  Obj* o = static_cast<Obj*>(malloc(size));
  if (!o) errorCode(ERR_MEMORY, "out of memory");
  o->gcNext = objects_;
  o->type = t;
  o->marked = 0;
  o->flags = 0;
  o->size = (uint32_t)size;
  objects_ = o;
  bytes_ += size;
  if (bytes_ > nextGC_) gcRequested_ = true;
  return o;
}

void VM::accountExternal(int64_t delta) {
  if (delta > 0) {
    if (bytes_ + (size_t)delta > cfg_.memoryLimit && nativeDepth_ == 0 && gcInhibit_ == 0) collectGarbage();
    if (bytes_ + (size_t)delta > cfg_.memoryLimit)
      errorCode(ERR_MEMORY, "out of memory (limit %zu bytes)", cfg_.memoryLimit);
    bytes_ += (size_t)delta;
    if (bytes_ > nextGC_) gcRequested_ = true;
  } else {
    if (bytes_ > peakBytes_) peakBytes_ = bytes_;
    bytes_ -= (size_t)(-delta);
  }
}

StrObj* VM::allocString(size_t len) {
  if (len > 0x3FFFFFFF) error("string too long");
  auto* s = static_cast<StrObj*>(allocRaw(OT::String, sizeof(StrObj) + len * 2));
  s->len = (uint32_t)len;
  s->hash = 0;
  return s;
}
StrObj* VM::newString(const char16_t* p, size_t len) {
  StrObj* s = allocString(len);
  if (len) memcpy(s->chars(), p, len * 2);
  return s;
}
StrObj* VM::newStringUtf8(const std::string& s) { return newString(utf8ToUtf16(s)); }
StrObj* VM::newStringAscii(const char* p) {
  size_t n = strlen(p);
  StrObj* s = allocString(n);
  for (size_t i = 0; i < n; i++) s->chars()[i] = (unsigned char)p[i];
  return s;
}

ArrObj* VM::newArray(uint8_t vt, uint32_t cap) {
  NoGC ng(*this);
  auto* a = static_cast<ArrObj*>(allocRaw(OT::Array, sizeof(ArrObj)));
  a->vt = vt;
  a->len = 0;
  a->cap = 0;
  a->data = nullptr;
  if (cap) arrayReserve(a, cap);
  return a;
}
void VM::arrayReserve(ArrObj* a, uint32_t cap) {
  if (cap <= a->cap) return;
  if (cap > 0x7FFFFFF) error("array too large");
  accountExternal((int64_t)(cap - a->cap) * 8);
  Slot* nd = static_cast<Slot*>(realloc(a->data, (size_t)cap * sizeof(Slot)));
  if (!nd) {
    accountExternal(-(int64_t)(cap - a->cap) * 8);
    errorCode(ERR_MEMORY, "out of memory");
  }
  memset(nd + a->cap, 0, (size_t)(cap - a->cap) * sizeof(Slot));
  a->data = nd;
  a->cap = cap;
}
void VM::arrayPush(ArrObj* a, Slot v) {
  if (a->len == a->cap) arrayReserve(a, a->cap < 4 ? 4 : a->cap + a->cap / 2);
  a->data[a->len++] = v;
}

BufObj* VM::newBuffer(uint32_t len) {
  NoGC ng(*this);
  auto* b = static_cast<BufObj*>(allocRaw(OT::Buffer, sizeof(BufObj)));
  b->len = 0;
  b->data = nullptr;
  accountExternal(len);
  b->data = static_cast<uint8_t*>(calloc(len ? len : 1, 1));
  if (!b->data) { accountExternal(-(int64_t)len); errorCode(ERR_MEMORY, "out of memory"); }
  b->len = len;
  return b;
}
BytesObj* VM::newBytesView(BufObj* buf, uint32_t off, uint32_t len) {
  auto* v = static_cast<BytesObj*>(allocRaw(OT::Bytes, sizeof(BytesObj)));
  v->buf = buf;
  v->off = off;
  v->len = len;
  return v;
}
BytesObj* VM::newBytes(uint32_t len) {
  NoGC ng(*this);
  BufObj* b = newBuffer(len);
  return newBytesView(b, 0, len);
}
BytesObj* VM::newBytesFrom(const uint8_t* p, size_t len) {
  NoGC ng(*this);
  if (len > 0x7FFFFFFF) error("byte array too large");
  BytesObj* v = newBytes((uint32_t)len);
  if (len) memcpy(v->ptr(), p, len);
  return v;
}

InstObj* VM::newInstance(ClassRT* cls) {
  size_t sz = sizeof(InstObj) + (size_t)cls->nfields * sizeof(Slot);
  auto* o = static_cast<InstObj*>(allocRaw(OT::Instance, sz));
  o->cls = cls;
  memset(o->fields(), 0, (size_t)cls->nfields * sizeof(Slot));
  return o;
}
CellObj* VM::newCell(uint8_t vt) {
  auto* c = static_cast<CellObj*>(allocRaw(OT::Cell, sizeof(CellObj)));
  c->vt = vt;
  c->v.u = 0;
  return c;
}
BoxObj* VM::newBox(uint8_t vt, Slot v) {
  auto* b = static_cast<BoxObj*>(allocRaw(OT::Box, sizeof(BoxObj)));
  b->vt = vt;
  b->v = v;
  return b;
}
CloObj* VM::newClosure(FuncRT* fn, uint32_t nup) {
  auto* c = static_cast<CloObj*>(allocRaw(OT::Closure, sizeof(CloObj) + nup * sizeof(CellObj*)));
  c->fn = fn;
  c->nup = nup;
  for (uint32_t i = 0; i < nup; i++) c->ups()[i] = nullptr;
  return c;
}
MapObj* VM::newMap(uint8_t kvt, uint8_t vvt, KeyMode km, bool isSet) {
  void* mem = allocRaw(OT::Map, sizeof(MapObj));
  Obj hdr = *static_cast<Obj*>(mem);
  auto* m = new (mem) MapObj();
  *static_cast<Obj*>(m) = hdr;
  m->kvt = kvt;
  m->vvt = vvt;
  m->km = km;
  m->isSet = isSet;
  m->count = 0;
  return m;
}
BigObj* VM::newBig() {
  void* mem = allocRaw(OT::BigInt, sizeof(BigObj));
  Obj hdr = *static_cast<Obj*>(mem);
  auto* b = new (mem) BigObj();
  *static_cast<Obj*>(b) = hdr;
  b->neg = false;
  return b;
}
HandleObj* VM::newHandle(int64_t id) {
  auto* h = static_cast<HandleObj*>(allocRaw(OT::Handle, sizeof(HandleObj)));
  h->id = id;
  return h;
}

void VM::pin(Obj* o) {
  if (o) pins_[o]++;
}
void VM::unpin(Obj* o) {
  auto it = pins_.find(o);
  if (it != pins_.end() && --it->second == 0) pins_.erase(it);
}

void VM::freeObj(Obj* o) {
  bytes_ -= o->size;
  switch (o->type) {
    case OT::Array: {
      auto* a = static_cast<ArrObj*>(o);
      bytes_ -= (size_t)a->cap * 8;
      free(a->data);
      break;
    }
    case OT::Buffer: {
      auto* b = static_cast<BufObj*>(o);
      bytes_ -= b->len;
      free(b->data);
      break;
    }
    case OT::Map: {
      auto* m = static_cast<MapObj*>(o);
      bytes_ -= m->entries.capacity() * sizeof(MapEntry) + m->buckets.capacity() * 4;
      m->~MapObj();
      break;
    }
    case OT::BigInt: {
      auto* b = static_cast<BigObj*>(o);
      bytes_ -= b->mag.capacity() * sizeof(uint32_t);
      b->~BigObj();
      break;
    }
    default: break;
  }
  free(o);
}

void VM::markRoots() {
  for (auto& mp : modules) {
    ModuleRT* m = mp.get();
    for (StrObj* s : m->strs) mark(s);
    for (size_t i = 0; i < m->globals.size(); i++)
      if (m->m.globals[i].vt == VT_REF) mark(m->globals[i].r);
  }
  for (const Frame& fr : frames_) {
    Slot* R = stack_ + fr.base;
    for (uint16_t r : fr.fn->refRegs) mark(R[r].r);
    mark(fr.closure);
  }
  if (resultKind_ == 'R') mark(result_.r);
  for (Obj* o : tempRoots_) mark(o);
  for (auto& kv : pins_) mark(kv.first);
}

void VM::traceAll() {
  while (!gray_.empty()) {
    Obj* o = gray_.back();
    gray_.pop_back();
    switch (o->type) {
      case OT::Array: {
        auto* a = static_cast<ArrObj*>(o);
        if (a->vt == VT_REF)
          for (uint32_t i = 0; i < a->len; i++) mark(a->data[i].r);
        break;
      }
      case OT::Bytes: mark(static_cast<BytesObj*>(o)->buf); break;
      case OT::Instance: {
        auto* in = static_cast<InstObj*>(o);
        for (uint16_t f : in->cls->refFields) mark(in->fields()[f].r);
        break;
      }
      case OT::Closure: {
        auto* c = static_cast<CloObj*>(o);
        for (uint32_t i = 0; i < c->nup; i++) mark(c->ups()[i]);
        break;
      }
      case OT::Cell: {
        auto* c = static_cast<CellObj*>(o);
        if (c->vt == VT_REF) mark(c->v.r);
        break;
      }
      case OT::Box: {
        auto* b = static_cast<BoxObj*>(o);
        if (b->vt == VT_REF) mark(b->v.r);
        break;
      }
      case OT::Map: {
        auto* m = static_cast<MapObj*>(o);
        bool kr = m->kvt == VT_REF, vr = m->vvt == VT_REF;
        if (kr || vr)
          for (auto& e : m->entries) {
            if (!e.live) continue;
            if (kr) mark(e.k.r);
            if (vr) mark(e.v.r);
          }
        break;
      }
      default: break;
    }
  }
}

void VM::sweep() {
  Obj** link = &objects_;
  while (Obj* o = *link) {
    if (o->marked) {
      o->marked = 0;
      link = &o->gcNext;
    } else {
      *link = o->gcNext;
      freeObj(o);
    }
  }
}

void VM::collectGarbage() {
  auto t0 = std::chrono::steady_clock::now();
  if (bytes_ > peakBytes_) peakBytes_ = bytes_;
  markRoots();
  traceAll();
  sweep();
  gcCount_++;
  gcRequested_ = false;
  uint64_t ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
  gcTotalNs_ += ns;
  if (ns > gcMaxNs_) gcMaxNs_ = ns;
  uint32_t growth = cfg_.gcGrowthPercent < 125 ? 125 : cfg_.gcGrowthPercent;
  size_t next = (size_t)((unsigned long long)bytes_ * growth / 100);
  if (next < (1u << 20)) next = 1u << 20;
  size_t cap = cfg_.memoryLimit - cfg_.memoryLimit / 8;
  if (next > cap && bytes_ < cap) next = cap;
  nextGC_ = next;
}

void VM::maybeGC() {
  if (gcRequested_ && nativeDepth_ == 0) collectGarbage();
}

// ------------------------------------------------------------------ loading
ModuleRT* VM::loadModule(Module&& mod) {
  std::string err;
  if (!verifyModule(mod, err)) errorCode(ERR_VERIFY, "bytecode verification failed: %s", err.c_str());
  auto mrt = std::make_unique<ModuleRT>();
  ModuleRT* M = mrt.get();
  M->m = std::move(mod);
  Module& m = M->m;
  for (auto& s : m.sigs) M->sigMap.push_back(sigId(s));
  // natives
  for (auto& ni : m.natives) {
    const NativeEntry* ne = findNative(ni.name);
    if (!ne) errorCode(ERR_LOAD, "unknown native function '%s'", ni.name.c_str());
    if (ne->sig != m.sigs[ni.sig])
      errorCode(ERR_LOAD, "native '%s' signature mismatch (module %s, host %s)", ni.name.c_str(),
                m.sigs[ni.sig].c_str(), ne->sig.c_str());
    NativeBinding nb{ne->fn, ne->ud, sigRet(ne->sig), ni.name};
    nb.cost = ne->cost;
    std::string ps = sigParams(ne->sig);
    for (size_t k = 0; k < ps.size() && k < 64; k++)
      if (ps[k] == 'R') nb.refArgs |= 1ull << k;
    M->natives.push_back(std::move(nb));
  }
  // functions
  M->funcs.resize(m.funcs.size());
  for (size_t i = 0; i < m.funcs.size(); i++) {
    FuncRT& f = M->funcs[i];
    const FuncDesc& d = m.funcs[i];
    f.d = &d;
    f.mod = M;
    f.name = d.name;
    f.nregs = (uint32_t)d.regKinds.size();
    f.nparams = d.nparams;
    f.sigId = M->sigMap[d.sig];
    f.retKind = sigRet(m.sigs[d.sig]);
    f.code = d.code.data();
    f.codeLen = d.code.size();
    for (uint32_t r = 0; r < f.nregs; r++)
      if (d.regKinds[r] == K_R) {
        f.refRegs.push_back((uint16_t)r);
        if (r >= d.nparams) f.refInit.push_back((uint16_t)r);
      }
    f.ics.resize(d.icNames.size());
    for (size_t k = 0; k < d.icNames.size(); k++) f.ics[k].name = d.icNames[k];
  }
  // classes
  M->classes.resize(m.classes.size());
  for (size_t i = 0; i < m.classes.size(); i++) {
    ClassRT& c = M->classes[i];
    const ClassDesc& d = m.classes[i];
    c.name = d.name;
    c.isRecord = d.isRecord != 0;
    c.isTuple = d.isRecord == 2;
    c.parent = d.parent >= 0 ? &M->classes[d.parent] : nullptr;
    c.nfields = (uint32_t)d.fields.size();
    for (size_t k = 0; k < d.fields.size(); k++) {
      c.fieldVT.push_back(d.fields[k].vt);
      c.fieldNames.push_back(d.fields[k].name);
      c.fieldIndex[d.fields[k].name] = (uint32_t)k;
      if (d.fields[k].vt == VT_REF) c.refFields.push_back((uint16_t)k);
    }
    for (size_t k = 0; k < d.vtable.size(); k++) {
      c.vtable.push_back(&M->funcs[d.vtable[k]]);
      c.methodNames.push_back(d.methodNames[k]);
      c.methodIndex[d.methodNames[k]] = (uint32_t)k;
    }
  }
  // string constants (rooted through the module)
  modules.push_back(std::move(mrt));
  M->strs.reserve(m.strs.size());
  for (auto& s : m.strs) M->strs.push_back(newString(s));
  M->globals.assign(m.globals.size(), Slot{});
  if (dbg_) dbg_->moduleLoaded(M);
  return M;
}

int VM::runInit(ModuleRT* mod) {
  if (mod->initialized) return ERR_OK;
  int st = callEntry(&mod->funcs[mod->m.initFunc], nullptr, 0);
  if (st == ERR_OK) mod->initialized = true;
  return st;
}

// ------------------------------------------------------------------ entry
int VM::callEntry(FuncRT* f, const Slot* args, uint32_t nargs) {
  if (suspended_ || !frames_.empty()) errorCode(ERR_STATE, "VM is busy (suspended or running)");
  if (nargs != f->nparams) errorCode(ERR_ARGS, "function '%s' expects %u arguments, got %u", f->name.c_str(), f->nparams, nargs);
  if (f->nregs > cfg_.stackSlots) errorCode(ERR_STACK, "stack overflow");
  Slot* R = stack_;
  for (uint32_t i = 0; i < nargs; i++) R[i] = args[i];
  for (uint16_t r : f->refInit) R[r].r = nullptr;
  frames_.push_back(Frame{f, f->code, nullptr, 0, kNoReg, true});
  resultKind_ = 'V';
  if (gcRequested_) collectGarbage();
  return runFrames();
}

// Runs the frames on the stack until the entry frame returns, the script suspends (budget) or a
// debugger pauses it (ERR_PAUSED: the frames are kept for inspection).
int VM::runFrames() {
  try {
    int st = execute(0);
    if (st == ERR_SUSPENDED || st == ERR_PAUSED) suspended_ = true;
    else frames_.clear();
    return st;
  } catch (E7Error&) {
    if (dbg_ && dbg_->takeErrorPause()) {
      // a debugger in pause-to-host mode reports the error first: keep the frames for inspection,
      // the error is raised again by resume()
      nativeDepth_ = 0;
      gcInhibit_ = 0;
      tempRoots_.clear();
      curNative_ = nullptr;
      callbackNatives_.clear();
      suspended_ = true;
      return ERR_PAUSED;
    }
    resetExecution();
    throw;
  } catch (...) {
    resetExecution();
    throw;
  }
}

int VM::resume() {
  if (!suspended_) errorCode(ERR_STATE, "VM is not suspended");
  if (dbg_ && dbg_->errorPending()) {
    // the error reported to the debugger now terminates the call
    E7Error e(dbg_->pendingErrorCode(), dbg_->pendingErrorMessage());
    resetExecution();
    throw e;
  }
  if (dbg_ && dbg_->hostPaused()) dbg_->prepareResume(0 /* continue */);
  finishPendingCatch();
  suspended_ = false;
  return runFrames();
}

void VM::resetExecution() {
  pendingCatch_ = PendingCatch{};
  frames_.clear();
  gcInhibit_ = 0;
  suspended_ = false;
  nativeDepth_ = 0;
  tempRoots_.clear();
  curNative_ = nullptr;
  callbackNatives_.clear();
  if (dbg_) dbg_->reset();
}

void VM::attachDebugger(std::unique_ptr<Debugger> d) {
  if (dbg_) detachDebugger();
  dbg_ = std::move(d);
  if (dbg_) dbg_->attached();
}

void VM::detachDebugger() {
  if (!dbg_) return;
  dbg_->detaching();
  dbg_.reset();
  dbgMask_.store(0);
}

Slot VM::callClosure(CloObj* c, const Slot* args, uint32_t nargs) {
  FuncRT* f = c->fn;
  // each re-entry uses native stack; bound it independently of the script frame limit
  if (nativeDepth_ > 64) errorCode(ERR_STACK, "too many nested native callbacks");
  if (nargs != f->nparams) error("callback expects %u arguments, got %u", f->nparams, nargs);
  uint32_t base = 0;
  if (!frames_.empty()) base = frames_.back().base + frames_.back().fn->nregs;
  if ((size_t)base + f->nregs > cfg_.stackSlots || frames_.size() >= cfg_.maxFrames)
    errorCode(ERR_STACK, "stack overflow");
  Slot* R = stack_ + base;
  for (uint32_t i = 0; i < nargs; i++) R[i] = args[i];
  for (uint16_t r : f->refInit) R[r].r = nullptr;
  size_t depth = frames_.size();
  frames_.push_back(Frame{f, f->code, c, base, kNoReg, true});
  // remember which native called back into script (for stack traces); while the callback runs,
  // no native is current
  callbackNatives_.push_back({depth, curNative_});
  const NativeBinding* saved = curNative_;
  curNative_ = nullptr;
  int st = execute(depth);
  (void)st;
  curNative_ = saved;
  callbackNatives_.pop_back();
  return result_;
}

bool VM::callClosureCatching(CloObj* c, std::string& errorMessage) {
  size_t depth = frames_.size();
  uint32_t nd = nativeDepth_;
  const NativeBinding* cn = curNative_;
  size_t cbs = callbackNatives_.size();
  size_t roots = tempRoots_.size();
  int inhibit = gcInhibit_;
  // errors raised inside are caught here: a debugger does not report them as terminating
  struct CatchScope {
    int& d;
    explicit CatchScope(int& x) : d(x) { d++; }
    ~CatchScope() { d--; }
  } scope(catchDepth_);
  try {
    callClosure(c, nullptr, 0);
    return true;
  } catch (E7Error& e) {
    if (e.code != ERR_RUNTIME) throw;
    frames_.erase(frames_.begin() + (std::ptrdiff_t)depth, frames_.end());
    nativeDepth_ = nd;
    curNative_ = cn;
    callbackNatives_.resize(cbs);
    tempRoots_.resize(roots);
    gcInhibit_ = inhibit;
    errorMessage = e.msg;
    return false;
  }
}

}  // namespace e7
