#include "vm.h"

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
  stack_ = static_cast<Slot*>(calloc(cfg_.stackSlots, sizeof(Slot)));
  if (!stack_) throw std::bad_alloc();
  frames_.reserve(cfg_.maxFrames + 1);
  undef_ = static_cast<Obj*>(allocRaw(OT::Undefined, sizeof(Obj)));
  pin(undef_);
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

void VM::registerNative(const std::string& name, const std::string& sig, NativeFn fn, void* ud) {
  natives_[name] = NativeEntry{sig, fn, ud};
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
void VM::raise(int code, const std::string& msg) {
  std::string full = msg;
  std::string tr = stackTrace();
  if (!tr.empty()) full += "\n" + tr;
  throw E7Error(code, full);
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

static uint32_t lineFor(const FuncRT* f, const uint64_t* ip) {
  if (!f || !ip || !f->d) return 0;
  ptrdiff_t pc = ip - f->code - 1;
  if (pc < 0) pc = 0;
  uint32_t line = 0;
  const auto& L = f->d->lines;
  for (size_t i = 0; i + 1 < L.size(); i += 2) {
    if ((ptrdiff_t)L[i] > pc) break;
    line = L[i + 1];
  }
  return line;
}

std::string VM::stackTrace() const {
  std::string out;
  size_t n = frames_.size();
  size_t shown = 0;
  for (size_t i = n; i-- > 0;) {
    const Frame& fr = frames_[i];
    if (shown == 16) { out += "    ...\n"; break; }
    char buf[300];
    snprintf(buf, sizeof buf, "    at %s (%s:%u)\n", fr.fn->name.c_str(), fr.fn->mod->m.name.c_str(),
             lineFor(fr.fn, fr.ip));
    out += buf;
    shown++;
  }
  if (!out.empty()) out.pop_back();
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
  if (!nd) errorCode(ERR_MEMORY, "out of memory");
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
  markRoots();
  traceAll();
  sweep();
  gcCount_++;
  gcRequested_ = false;
  size_t next = bytes_ * 2;
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
    M->natives.push_back(NativeBinding{ne->fn, ne->ud, sigRet(ne->sig), ni.name});
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
  try {
    int st = execute(0);
    if (st == ERR_SUSPENDED) suspended_ = true;
    else frames_.clear();
    return st;
  } catch (...) {
    frames_.clear();
    nativeDepth_ = 0;
    gcInhibit_ = 0;
    tempRoots_.clear();
    suspended_ = false;
    throw;
  }
}

int VM::resume() {
  if (!suspended_) errorCode(ERR_STATE, "VM is not suspended");
  suspended_ = false;
  try {
    int st = execute(0);
    if (st == ERR_SUSPENDED) suspended_ = true;
    else frames_.clear();
    return st;
  } catch (...) {
    frames_.clear();
    nativeDepth_ = 0;
    tempRoots_.clear();
    throw;
  }
}

void VM::resetExecution() {
  frames_.clear();
  gcInhibit_ = 0;
  suspended_ = false;
  nativeDepth_ = 0;
  tempRoots_.clear();
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
  int st = execute(depth);
  (void)st;
  return result_;
}

}  // namespace e7
