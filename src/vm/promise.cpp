// Promises and the microtask queue (DESIGN-0.3.md D2).
//
// The VM implements the jobs of the ECMAScript specification (PromiseReactionJob,
// PromiseResolveThenableJob) and processes them in the specification's order, so the order in which
// callbacks run is the one Node.js shows. There is no event loop: a call runs its frames, then the
// queued jobs until none is left. Host operations are promises settled through tickets; while an
// async entry call waits for one, the call returns ERR_PENDING and the host resumes it after
// settling tickets.
#include <cmath>

#include "../stdlib/util.h"
#include "debug.h"
#include "vm.h"

namespace e7 {

PromiseObj* VM::newPromise(uint8_t vt) {
  void* mem = allocRaw(OT::Promise, sizeof(PromiseObj));
  // allocRaw initialized the Obj header; construct the rest in place (the reaction vector)
  auto* p = static_cast<PromiseObj*>(mem);
  new (&p->reactions) std::vector<Reaction>();
  p->state = PS_PENDING;
  p->vt = vt;
  p->handled = false;
  p->alreadyResolved = false;
  p->remaining = 0;
  p->classIdx = -1;
  p->mod = nullptr;
  p->value.u = 0;
  return p;
}

static bool isPromise(Obj* o) { return o && o->type == OT::Promise; }

void VM::fulfillPromise(PromiseObj* p, Slot v) {
  if (p->state != PS_PENDING) return;
  std::vector<Reaction> rs;
  rs.swap(p->reactions);
  p->state = PS_FULFILLED;
  p->value = v;
  for (auto& r : rs) microtasks_.push_back(Job{r, v, p->vt, false});
}

void VM::rejectPromise(PromiseObj* p, Obj* reason) {
  if (p->state != PS_PENDING) return;
  std::vector<Reaction> rs;
  rs.swap(p->reactions);
  p->state = PS_REJECTED;
  p->value.r = reason;
  if (!p->handled) unhandled_.push_back(p);  // HostPromiseRejectionTracker(reject)
  Slot a;
  a.r = reason;
  for (auto& r : rs) microtasks_.push_back(Job{r, a, VT_REF, true});
}

void VM::resolvePromise(PromiseObj* p, Slot v) {
  if (p->state != PS_PENDING) return;
  // a value is never a promise (Promise<Promise<T>> is Promise<T>), so a promise here is a thenable to
  // adopt: NewPromiseResolveThenableJob
  if (kindOfVT(p->vt) == K_R && isPromise(v.r)) {
    adoptPromise(p, static_cast<PromiseObj*>(v.r));
    return;
  }
  fulfillPromise(p, v);
}

void VM::adoptPromise(PromiseObj* p, PromiseObj* thenable) {
  if (p->state != PS_PENDING) return;
  if (thenable == p) {
    rejectPromise(p, newErrorObject("TypeError", "Chaining cycle detected for promise #<Promise>"));
    return;
  }
  Reaction r;
  r.kind = RK_THENABLE;
  r.a = thenable;
  r.derived = p;
  microtasks_.push_back(Job{r, Slot{}, VT_REF, false});
}

void VM::promiseThen(PromiseObj* p, const Reaction& r) {
  if (p->state == PS_PENDING) {
    p->reactions.push_back(r);
  } else if (p->state == PS_FULFILLED) {
    microtasks_.push_back(Job{r, p->value, p->vt, false});
  } else {
    if (!p->handled) {  // HostPromiseRejectionTracker(handle)
      for (size_t i = 0; i < unhandled_.size(); i++)
        if (unhandled_[i] == p) { unhandled_.erase(unhandled_.begin() + (std::ptrdiff_t)i); break; }
    }
    microtasks_.push_back(Job{r, p->value, VT_REF, true});
  }
  p->handled = true;
}

int64_t VM::newTicket(PromiseObj* p, void* hostData) {
  int64_t t = nextTicket_++;
  tickets_[t] = {p, hostData};
  return t;
}

bool VM::ticketHostData(int64_t ticket, void** data) const {
  auto it = tickets_.find(ticket);
  if (it == tickets_.end()) return false;
  *data = it->second.second;
  return true;
}

void VM::newAutoTicket(PromiseObj* p, bool ok, Slot v, uint8_t vt) {
  int64_t t = newTicket(p);
  autoTickets_.push_back(AutoTicket{t, ok, v, vt});
}

bool VM::settleNextAutoTicket() {
  while (!autoTickets_.empty()) {
    AutoTicket a = autoTickets_.front();
    autoTickets_.pop_front();
    if (settleTicket(a.ticket, a.ok, a.v, a.ok ? nullptr : a.v.r)) return true;
  }
  return false;
}

bool VM::settleTicket(int64_t ticket, bool ok, Slot value, Obj* reason) {
  auto it = tickets_.find(ticket);
  if (it == tickets_.end()) return false;
  PromiseObj* p = it->second.first;
  tickets_.erase(it);
  if (ok) resolvePromise(p, value);
  else rejectPromise(p, reason);
  return true;
}

std::string VM::rejectionMessage(Obj* reason) const {
  StrObj *n, *m;
  if (!errorParts(reason, &n, &m)) return "Uncaught (in promise)";
  std::string name = strUtf8(n), text = strUtf8(m);
  return name == "Error" ? text : name + ": " + text;
}

Obj* VM::errorValue(E7Error& e) {
  return e.value ? e.value : newErrorObject(e.jsName, e.text);
}

// The outcome of a reaction without script code: settles r.derived (pass-through, thenable adoption,
// finally's value thunk, the combinators)
void VM::settleReaction(const Reaction& r, bool rejected, Slot v, uint8_t vt) {
  PromiseObj* d = r.derived;
  switch (r.kind) {
    case RK_THEN:
    case RK_THENABLE:
      if (!d) return;
      if (rejected) rejectPromise(d, v.r);
      else if (kindOfVT(vt) != kindOfVT(d->vt)) error("promise value type mismatch");
      else resolvePromise(d, v);
      return;
    case RK_FINALLY_VALUE:
      // onFinally() settled (fulfilled): continue with the source's outcome; it was rejected: that wins
      if (rejected) rejectPromise(d, v.r);
      else if (r.capturedRejected) rejectPromise(d, r.captured.r);
      else resolvePromise(d, r.captured);
      return;
    case RK_ALL: {
      if (rejected) { rejectPromise(d, v.r); return; }
      auto* arr = static_cast<ArrObj*>(r.agg);
      if (r.index < arr->len) arr->data[r.index] = v;
      if (--d->remaining == 0) {
        Slot a;
        a.r = arr;
        fulfillPromise(d, a);
      }
      return;
    }
    case RK_ALL_SETTLED: {
      auto* arr = static_cast<ArrObj*>(r.agg);
      auto* mod = static_cast<ModuleRT*>(d->mod);
      if (!mod || d->classIdx < 0 || (size_t)d->classIdx >= mod->classes.size()) error("bad allSettled record class");
      ClassRT* c = &mod->classes[(size_t)d->classIdx];
      auto fs = c->fieldIndex.find("status"), fv = c->fieldIndex.find("value"), fr = c->fieldIndex.find("reason");
      if (fs == c->fieldIndex.end() || fv == c->fieldIndex.end() || fr == c->fieldIndex.end() || !c->presence ||
          c->fieldVT[fs->second] != VT_REF || c->fieldVT[fr->second] != VT_REF || c->fieldVT[fv->second] != VT_REF)
        error("bad allSettled record class");
      // the value field is `T | undefined`: a primitive is boxed
      if (!rejected && vt != VT_REF) {
        TempRoot keepArr0(*this, arr);
        v.r = newBox(vt == VT_NUMBER ? (uint8_t)VT_NUMBER : vt, v);
        vt = VT_REF;
      }
      TempRoot keepArr(*this, arr);
      Obj* keepV = kindOfVT(vt) == K_R ? v.r : nullptr;
      TempRoot keepVal(*this, keepV);
      StrObj* st = newStringAscii(rejected ? "rejected" : "fulfilled");
      TempRoot keepSt(*this, st);
      InstObj* rec = newInstance(c);
      rec->fields()[fs->second].r = st;
      if (rejected) {
        rec->fields()[fr->second].r = v.r;
        if (c->fieldVT[fv->second] == VT_REF) rec->fields()[fv->second].r = undef_;
        setFieldPresent(rec, fv->second, false);
      } else {
        rec->fields()[fv->second] = v;
        rec->fields()[fr->second].r = undef_;
        setFieldPresent(rec, fr->second, false);
      }
      if (r.index < arr->len) arr->data[r.index].r = rec;
      if (--d->remaining == 0) {
        Slot a;
        a.r = arr;
        fulfillPromise(d, a);
      }
      return;
    }
    case RK_ANY: {
      if (!rejected) { resolvePromise(d, v); return; }
      auto* arr = static_cast<ArrObj*>(r.agg);
      if (r.index < arr->len) arr->data[r.index] = v;
      if (--d->remaining == 0) {
        TempRoot keepArr(*this, arr);
        ArrObj* e = newErrorObject("AggregateError", "All promises were rejected");
        TempRoot keepE(*this, e);
        // its errors: the reasons, in the order of the promises
        arrayPush(e, Slot{});
        e->data[2].r = undef_;
        arrayPush(e, Slot{});
        e->data[3].r = arr;
        rejectPromise(d, e);
      }
      return;
    }
    case RK_RACE:
      if (rejected) rejectPromise(d, v.r);
      else resolvePromise(d, v);
      return;
    default:
      return;
  }
}

// Starts a job: pushes the frame of the script code it runs (an await continuation, a then / catch /
// finally callback), or does all of its work at once (returns false).
bool VM::startJob(Job& j) {
  const Reaction& r = j.r;
  switch (r.kind) {
    case RK_AWAIT: {
      auto* co = static_cast<CoroObj*>(r.a);
      if (!co || co->type != OT::Coro || co->state != CO_SUSPENDED) return false;
      FuncRT* cf = co->fn;
      if ((size_t)cf->nregs > cfg_.stackSlots) errorCode(ERR_STACK, "stack overflow");
      Slot* NR = stack_;
      memcpy(NR, co->regs(), (size_t)cf->nregs * sizeof(Slot));
      const uint64_t* aw = cf->code + co->pc;  // the AWAIT
      uint16_t dst = insA(aw[0]);
      if (!j.rejected && dst != kNoReg) {
        if (cf->d->regKinds[dst] != kindOfVT(j.argVT)) errorCode(ERR_RUNTIME, "await: promise value type mismatch");
        NR[dst] = j.arg;
      }
      co->state = CO_RUNNING;
      frames_.push_back(Frame{cf, aw + 1, co->closure, 0, kNoReg, true, co, kNoReg});
      curJob_ = JOB_AWAIT;
      if (j.rejected) pendingThrow_ = j.arg.r;  // raised at the AWAIT when execution starts
      return true;
    }
    case RK_THEN: {
      Obj* h = j.rejected ? r.b : r.a;
      if (!h) {
        settleReaction(r, j.rejected, j.arg, j.argVT);
        return false;
      }
      break;
    }
    case RK_FINALLY:
      break;
    case RK_THENABLE: {
      // PromiseResolveThenableJob: thenable.then(resolve, reject) of the promise being resolved (a
      // reaction without handlers settles `derived` like the thenable)
      Reaction t;
      t.kind = RK_THEN;
      t.derived = r.derived;
      promiseThen(static_cast<PromiseObj*>(r.a), t);
      return false;
    }
    default:
      settleReaction(r, j.rejected, j.arg, j.argVT);
      return false;
  }
  // a callback: then / catch handler (with the value or reason), or finally's onFinally()
  Obj* h = r.kind == RK_FINALLY ? r.a : j.rejected ? r.b : r.a;
  if (!h || h->type != OT::Closure) error("promise callback is not a function");
  auto* clo = static_cast<CloObj*>(h);
  FuncRT* f = clo->fn;
  uint32_t argc = r.kind == RK_FINALLY ? 0 : f->nparams;
  if (argc > 1) error("promise callback takes too many arguments");
  if ((size_t)f->nregs > cfg_.stackSlots) errorCode(ERR_STACK, "stack overflow");
  Slot* R = stack_;
  if (argc == 1) {
    uint8_t want = j.rejected ? (uint8_t)K_R : kindOfVT(j.argVT);
    if (f->d->regKinds[0] != want) error("promise callback parameter type mismatch");
    R[0] = j.arg;
  }
  for (uint16_t k : f->refInit) R[k].r = nullptr;
  frames_.push_back(Frame{f, f->code, clo, 0, kNoReg, true});
  curJob_ = JOB_CALLBACK;
  curDerived_ = r.derived;
  curReaction_ = r;
  curRejected_ = j.rejected;
  curArg_ = j.arg;
  curArgVT_ = j.argVT;
  return true;
}

// A callback job's frame returned v (kind `kind`)
void VM::finishCallbackJob(Slot v, char kind) {
  PromiseObj* d = curDerived_;
  Reaction r = curReaction_;
  curJob_ = JOB_NONE;
  curDerived_ = nullptr;
  if (r.kind == RK_FINALLY) {
    // thenFinally / catchFinally: wait for onFinally()'s result, then settle like the source
    if (!d) return;
    PromiseObj* res;
    if (kind == 'R' && isPromise(v.r)) {
      res = static_cast<PromiseObj*>(v.r);
    } else {
      res = newPromise(VT_REF);
      Slot u;
      u.r = undef_;
      fulfillPromise(res, u);
    }
    TempRoot keep(*this, res);
    PromiseObj* p2 = newPromise(d->vt);
    TempRoot keep2(*this, p2);
    Reaction t;
    t.kind = RK_FINALLY_VALUE;
    t.derived = p2;
    t.captured = curArg_;
    t.capturedVT = curArgVT_;
    t.capturedRejected = curRejected_;
    promiseThen(res, t);
    Slot pv;
    pv.r = p2;
    resolvePromise(d, pv);  // d adopts p2 (a thenable)
    return;
  }
  if (!d) return;
  if (kind == 'R' && isPromise(v.r)) {
    adoptPromise(d, static_cast<PromiseObj*>(v.r));
    return;
  }
  if (kind == 'V') {
    // a callback that returns nothing (catch handlers): undefined, or the zero value of a primitive
    Kind k = kindOfVT(d->vt);
    if (k == K_R) v.r = undef_;
    else if (k == K_F) v.f = std::nan("");
    else v.i = 0;
  } else if (kindChar(kindOfVT(d->vt)) != kind) {
    error("promise callback returned a value of the wrong type");
  }
  resolvePromise(d, v);
}

bool VM::failCallbackJob(E7Error& e) {
  if (curJob_ != JOB_CALLBACK || e.jsName.empty()) return false;
  // the callback threw: its frames are gone, the promise it would have resolved is rejected
  dropFrames(0);
  nativeDepth_ = 0;
  gcBlockers_ = 0;
  gcInhibit_ = 0;
  tempRoots_.clear();
  curNative_ = nullptr;
  callbackNatives_.clear();
  PromiseObj* d = curDerived_;
  curJob_ = JOB_NONE;
  curDerived_ = nullptr;
  if (!d) return true;
  TempRoot keep(*this, d);
  Obj* err = errorValue(e);
  rejectPromise(d, err);
  return true;
}

// The call's frames have returned and the queue is empty: its result. An async entry function's
// promise gives the value (fulfilled), the error (rejected), or ERR_PENDING while host tickets are open.
int VM::finishTurn() {
  if (entryError_) {
    // the call's own uncaught error (its queued jobs have run)
    E7Error e = *entryError_;
    entryError_.reset();
    unhandled_.clear();
    throw e;
  }
  // unhandled rejections end the call, as they end the process in Node.js
  for (PromiseObj* p : unhandled_) {
    if (p->handled || p->state != PS_REJECTED) continue;
    if (entryKind_ == 'R' && entryResult_.r == p && entryFn_ && fflagsKind(entryFn_->d->fflags) == FK_ASYNC) continue;
    std::string msg = rejectionMessage(p->value.r);
    unhandled_.clear();
    raise(ERR_RUNTIME, msg, false);
  }
  unhandled_.clear();
  // host operations still open: the call waits for them (it is not finished until they are)
  if (!tickets_.empty()) {
    pending_ = true;
    return ERR_PENDING;
  }
  result_ = entryResult_;
  resultKind_ = entryKind_;
  if (entryFn_ && entryFn_->d && fflagsKind(entryFn_->d->fflags) == FK_ASYNC && entryKind_ == 'R' && isPromise(entryResult_.r)) {
    auto* p = static_cast<PromiseObj*>(entryResult_.r);
    if (p->state == PS_FULFILLED) {
      result_ = p->value;
      resultKind_ = kindOfVT(p->vt) == K_R ? 'R' : kindChar(kindOfVT(p->vt));
    } else if (p->state == PS_REJECTED) {
      std::string msg = rejectionMessage(p->value.r);
      raise(ERR_RUNTIME, msg, false);
    } else {
      raise(ERR_RUNTIME, "the async function never finishes: it awaits a promise that is never settled", false);
    }
  }
  entryFn_ = nullptr;
  entryResult_.u = 0;
  entryKind_ = 'V';
  return ERR_OK;
}

// ---------------------------------------------------------------- natives (since 0.3.0)
static PromiseObj* Pr(VM& vm, Slot s) {
  if (E7_UNLIKELY(!s.r || s.r->type != OT::Promise)) vm.error(s.r ? "expected a promise" : "cannot read properties of null or undefined (Promise)");
  return static_cast<PromiseObj*>(s.r);
}
static uint8_t vtArg(VM& vm, Slot s) {
  if (s.i < 0 || s.i >= VT__COUNT) vm.error("bad promise value type");
  return (uint8_t)s.i;
}
static uint8_t vtKindCheck(VM& vm, Slot s, Kind k) {
  uint8_t vt = vtArg(vm, s);
  if (kindOfVT(vt) != k) vm.error("bad promise value type");
  return vt;
}

// Promise.resolve(v) of a value (a promise argument is passed through by the compiler)
E7_NATIVE(n_promiseResolveI) { (void)n; (void)ud; PromiseObj* p = vm.newPromise(vtKindCheck(vm, a[1], K_I)); vm.fulfillPromise(p, a[0]); r->r = p; }
E7_NATIVE(n_promiseResolveF) { (void)n; (void)ud; PromiseObj* p = vm.newPromise(vtKindCheck(vm, a[1], K_F)); vm.fulfillPromise(p, a[0]); r->r = p; }
E7_NATIVE(n_promiseResolveR) { (void)n; (void)ud; PromiseObj* p = vm.newPromise(vtKindCheck(vm, a[1], K_R)); vm.resolvePromise(p, a[0]); r->r = p; }
E7_NATIVE(n_promiseReject) {
  (void)n; (void)ud;
  PromiseObj* p = vm.newPromise(vtArg(vm, a[1]));
  vm.rejectPromise(p, a[0].r);
  r->r = p;
}
// new Promise(executor): the promise; its resolve / reject functions call the natives below
E7_NATIVE(n_promiseNew) { (void)n; (void)ud; r->r = vm.newPromise(vtArg(vm, a[0])); }
static void resolveFn(VM& vm, Slot* a, Kind k) {
  PromiseObj* p = Pr(vm, a[0]);
  if (p->alreadyResolved) return;
  if (kindOfVT(p->vt) != k) vm.error("promise value type mismatch");
  p->alreadyResolved = true;
  vm.resolvePromise(p, a[1]);
}
E7_NATIVE(n_promiseResolveFnI) { (void)n; (void)ud; (void)r; resolveFn(vm, a, K_I); }
E7_NATIVE(n_promiseResolveFnF) { (void)n; (void)ud; (void)r; resolveFn(vm, a, K_F); }
E7_NATIVE(n_promiseResolveFnR) { (void)n; (void)ud; (void)r; resolveFn(vm, a, K_R); }
E7_NATIVE(n_promiseRejectFn) {
  (void)n; (void)ud; (void)r;
  PromiseObj* p = Pr(vm, a[0]);
  if (p->alreadyResolved) return;
  p->alreadyResolved = true;
  vm.rejectPromise(p, a[1].r);
}
// p.then(onFulfilled, onRejected) (either may be null): the derived promise has value type a[3]
E7_NATIVE(n_promiseThen) {
  (void)n; (void)ud;
  PromiseObj* p = Pr(vm, a[0]);
  for (int k = 1; k <= 2; k++)
    if (a[k].r && a[k].r->type != OT::Closure) vm.error("then: the callback is not a function");
  uint8_t dvt = vtArg(vm, a[3]);
  // without onFulfilled the value passes through to the derived promise: one representation
  if (!a[1].r && kindOfVT(dvt) != kindOfVT(p->vt)) vm.error("then: promise value type mismatch");
  PromiseObj* d = vm.newPromise(dvt);
  Reaction rr;
  rr.kind = RK_THEN;
  rr.a = a[1].r;
  rr.b = a[2].r;
  rr.derived = d;
  vm.promiseThen(p, rr);
  r->r = d;
}
E7_NATIVE(n_promiseFinally) {
  (void)n; (void)ud;
  PromiseObj* p = Pr(vm, a[0]);
  Clo(vm, a[1]);
  PromiseObj* d = vm.newPromise(p->vt);
  Reaction rr;
  rr.kind = RK_FINALLY;
  rr.a = a[1].r;
  rr.derived = d;
  vm.promiseThen(p, rr);
  r->r = d;
}
// Promise.all / allSettled / any / race over an array of promises; a[1] the elements' value type
static void combinator(VM& vm, Slot* a, Slot* r, uint8_t kind, int32_t classIdx) {
  ArrObj* arr = A(vm, a[0]);
  if (arr->vt != VT_REF) vm.error("expected an array of promises");
  uint8_t vt = vtArg(vm, a[1]);
  uint32_t len = arr->len;
  for (uint32_t i = 0; i < len; i++)
    if (!arr->data[i].r || arr->data[i].r->type != OT::Promise || (kind != RK_ALL_SETTLED && static_cast<PromiseObj*>(arr->data[i].r)->vt != vt))
      vm.error("expected an array of promises");
  uint8_t resVT = kind == RK_RACE || kind == RK_ANY ? vt : (uint8_t)VT_REF;
  PromiseObj* d = vm.newPromise(resVT);
  VM::TempRoot keepD(vm, d);
  ArrObj* agg = nullptr;
  if (kind != RK_RACE) {
    uint8_t avt = kind == RK_ALL ? vt : (uint8_t)VT_REF;
    agg = vm.newArray(avt, len);
    agg->len = len;
    for (uint32_t i = 0; i < len; i++) {
      if (avt == VT_REF) agg->data[i].r = vm.undef();
      else agg->data[i].u = 0;
    }
  }
  VM::TempRoot keepA(vm, agg);
  d->remaining = len;
  d->classIdx = classIdx;
  d->mod = vm.currentModule();
  if (len == 0) {
    if (kind == RK_ALL || kind == RK_ALL_SETTLED) {
      Slot v;
      v.r = agg;
      vm.fulfillPromise(d, v);
    } else if (kind == RK_ANY) {
      vm.rejectPromise(d, vm.newErrorObject("AggregateError", "All promises were rejected"));
    }
    r->r = d;
    return;
  }
  for (uint32_t i = 0; i < len; i++) {
    Reaction rr;
    rr.kind = kind;
    rr.index = i;
    rr.derived = d;
    rr.agg = agg;
    vm.promiseThen(static_cast<PromiseObj*>(arr->data[i].r), rr);
  }
  r->r = d;
}
E7_NATIVE(n_promiseAll) { (void)n; (void)ud; combinator(vm, a, r, RK_ALL, -1); }
E7_NATIVE(n_promiseAny) { (void)n; (void)ud; combinator(vm, a, r, RK_ANY, -1); }
E7_NATIVE(n_promiseRace) { (void)n; (void)ud; combinator(vm, a, r, RK_RACE, -1); }
E7_NATIVE(n_promiseAllSettled) {
  (void)n; (void)ud;
  if (a[2].i < 0 || a[2].i > INT32_MAX) vm.error("bad allSettled record class");
  combinator(vm, a, r, RK_ALL_SETTLED, (int32_t)a[2].i);
}

// e7:test later(v) / laterReject(e): a promise settled in a later turn (setImmediate in Node.js)
static void later(VM& vm, Slot* a, Slot* r, Kind k) {
  uint8_t vt = vtKindCheck(vm, a[1], k);
  PromiseObj* p = vm.newPromise(vt);
  vm.newAutoTicket(p, true, a[0], vt);
  r->r = p;
}
E7_NATIVE(n_testLaterI) { (void)n; (void)ud; later(vm, a, r, K_I); }
E7_NATIVE(n_testLaterF) { (void)n; (void)ud; later(vm, a, r, K_F); }
E7_NATIVE(n_testLaterR) { (void)n; (void)ud; later(vm, a, r, K_R); }
E7_NATIVE(n_testLaterReject) {
  (void)n; (void)ud;
  PromiseObj* p = vm.newPromise(vtArg(vm, a[1]));
  vm.newAutoTicket(p, false, a[0], VT_REF);
  r->r = p;
}

void registerPromiseNatives(VM& vm) {
  vm.registerNative("test.laterI", "II>R", n_testLaterI);
  vm.registerNative("test.laterF", "FI>R", n_testLaterF);
  vm.registerNative("test.laterR", "RI>R", n_testLaterR);
  vm.registerNative("test.laterReject", "RI>R", n_testLaterReject);
  vm.registerNative("Promise.resolveI", "II>R", n_promiseResolveI);
  vm.registerNative("Promise.resolveF", "FI>R", n_promiseResolveF);
  vm.registerNative("Promise.resolveR", "RI>R", n_promiseResolveR);
  vm.registerNative("Promise.reject", "RI>R", n_promiseReject);
  vm.registerNative("Promise.new", "I>R", n_promiseNew);
  vm.registerNative("Promise.resolveFnI", "RI>V", n_promiseResolveFnI);
  vm.registerNative("Promise.resolveFnF", "RF>V", n_promiseResolveFnF);
  vm.registerNative("Promise.resolveFnR", "RR>V", n_promiseResolveFnR);
  vm.registerNative("Promise.rejectFn", "RR>V", n_promiseRejectFn);
  vm.registerNative("Promise.then", "RRRI>R", n_promiseThen);
  vm.registerNative("Promise.finally", "RR>R", n_promiseFinally);
  vm.registerNative("Promise.all", "RI>R", n_promiseAll);
  vm.registerNative("Promise.allSettled", "RII>R", n_promiseAllSettled);
  vm.registerNative("Promise.any", "RI>R", n_promiseAny);
  vm.registerNative("Promise.race", "RI>R", n_promiseRace);
}

}  // namespace e7
