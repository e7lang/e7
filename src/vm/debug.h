// e7vm debugger core: breakpoints, stepping, pausing and inspection of a running VM.
//
// The VM runs a second instantiation of its interpreter while a debugger is attached; that one
// consults a per-instruction flag byte (FuncRT::dbgFlags) before each instruction and calls
// Debugger::check() when a flag bit is set in VM::dbgMask_. Without a debugger the normal
// interpreter runs and nothing here costs anything.
//
// Two ways to handle a stop:
//  * synchronous: a stop handler (onStop) is called on the VM thread with the VM frozen; it
//    inspects the state and returns how to continue. Works everywhere (also inside module
//    initialization and inside callbacks from natives such as Array.sort comparators).
//  * pause-to-host (no handler): the interpreter returns ERR_PAUSED, the host call (e7_load /
//    e7_call / e7_resume) returns E7_PAUSED with the frames kept, the host inspects at leisure and
//    continues with prepareResume() + VM::resume(). Built on the suspend/resume machinery; a stop
//    inside a callback from a native cannot be reported this way (the native's C++ frames are in
//    the way), so breakpoints there are skipped in this mode.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "vm.h"

namespace e7 {

enum DebugFlag : uint8_t {
  DF_STMT = 1,      // statement boundary (stepping, pause)
  DF_BP = 2,        // breakpoint
  DF_DEBUGGER = 4,  // `debugger;` statement (DEBUGGER instruction)
  DF_ALWAYS = 0x80  // set on every pc: forces a check at the next instruction
};

enum StopReason { SR_NONE = 0, SR_BREAKPOINT = 1, SR_STEP = 2, SR_DEBUGGER = 3, SR_PAUSE = 4, SR_EXCEPTION = 5, SR_ENTRY = 6 };
enum DebugAction { DA_CONTINUE = 0, DA_STEP_IN = 1, DA_STEP_OVER = 2, DA_STEP_OUT = 3, DA_ABORT = 4 };

struct DebugFrame {
  std::string function;
  SrcLoc loc;
};

struct DebugVar {
  std::string name, type, value;
  uint32_t ref = 0;    // > 0: expandable (pass to variables())
  size_t count = 0;    // number of children (elements / fields / entries)
};

struct DebugScope {
  std::string name;  // "Locals", "Closure", "Globals"
  uint32_t ref = 0;
  size_t count = 0;
};

struct Breakpoint {
  int id = 0;
  std::string file;
  uint32_t line = 0;        // requested line
  uint32_t actualLine = 0;  // line it was moved to (first line with a statement at or after `line`)
  bool verified = false;    // resolved to code in a loaded module
  std::vector<std::pair<const FuncRT*, uint32_t>> locs;
};

class Debugger {
 public:
  explicit Debugger(VM& vm) : vm_(vm) {}
  Debugger(const Debugger&) = delete;
  Debugger& operator=(const Debugger&) = delete;

  // Synchronous stop handler (empty: pause-to-host mode). Returns a DebugAction.
  std::function<int(int reason)> onStop;
  bool breakOnError = true;  // stop when a runtime error is about to terminate the script

  // ---- breakpoints (by source file and line; resolved now and when modules are loaded later)
  int addBreakpoint(const std::string& file, uint32_t line);
  bool removeBreakpoint(int id);
  void clearBreakpoints();
  const Breakpoint* breakpoint(int id) const;
  const std::vector<Breakpoint>& breakpoints() const { return bps_; }

  // ---- control
  void requestPause(int reason = SR_PAUSE);  // any thread: stop at the next statement
  bool paused() const { return paused_; }           // stopped (inside onStop, or returned to the host)
  bool hostPaused() const { return paused_ && hostPaused_; }
  // pause-to-host mode: how to continue (call before VM::resume())
  void prepareResume(int action);

  // ---- state of the current stop
  int reason() const { return reason_; }
  const std::string& message() const { return message_; }  // runtime error text (SR_EXCEPTION)
  const std::vector<int>& hitBreakpoints() const { return hits_; }

  // ---- inspection (valid while paused); frame 0 is the innermost
  size_t frameCount() const;
  DebugFrame frame(size_t i) const;
  std::vector<DebugScope> scopes(size_t frame);
  // children of a scope / object reference; count 0 = all
  std::vector<DebugVar> variables(uint32_t ref, size_t start = 0, size_t count = 0);
  // a variable name followed by .field / [index] / ['key'] / .length / .size
  bool evaluate(size_t frame, const std::string& expr, DebugVar& out, std::string& err);
  // source position (for listings)
  static bool pathMatches(const std::string& want, const std::string& file, size_t* score = nullptr);

  // ---- hooks called by the VM
  void attached();
  void detaching();
  void moduleLoaded(ModuleRT* m);
  int check(const uint64_t* ip);  // debug interpreter slow path; returns 0 or ERR_PAUSED
  void errorRaised(int code, const std::string& msg, const std::string& full, bool caught);
  bool takeErrorPause();  // host mode: an error stop is pending (VM keeps the frames)
  bool errorPending() const { return errorPending_; }
  int pendingErrorCode() const { return errCode_; }
  const std::string& pendingErrorMessage() const { return errFull_; }
  void reset();  // execution abandoned

 private:
  struct FuncInfo {
    std::vector<DebugPos> stmts;   // statement boundaries (from debug info, else from the line table)
    std::vector<std::string> files;  // file of each statement
  };
  struct Ref {
    int kind;  // 0 locals, 1 closure, 2 globals, 3 object
    size_t frame;
    Obj* obj;
  };

  VM& vm_;
  std::vector<Breakpoint> bps_;
  int nextBpId_ = 1;
  std::map<const FuncRT*, FuncInfo> info_;

  // stepping
  int stepMode_ = DA_CONTINUE;
  size_t stepDepth_ = 0;
  std::atomic<bool> pauseReq_{false};
  std::atomic<int> pauseReason_{SR_PAUSE};
  // resume point (pause-to-host): the instruction we stopped before is not checked again
  const uint64_t* skipIp_ = nullptr;
  size_t skipDepth_ = 0;

  // current stop
  bool paused_ = false, hostPaused_ = false, inHandler_ = false, aborting_ = false;
  int reason_ = SR_NONE;
  bool exact_ = true;  // top frame stopped before executing the instruction at ip (else: error inside it)
  std::string message_;
  std::vector<int> hits_;
  std::vector<Ref> refs_;
  std::map<Obj*, uint32_t> objRefs_;
  // host mode: error stop pending
  bool errorPending_ = false;
  int errCode_ = 0;
  std::string errFull_;

  void buildInfo(ModuleRT* m);
  void applyFlags();
  void resolve(Breakpoint& bp);
  void updateMask();
  int stop(int reason, const uint64_t* ip, bool exact);
  void applyAction(int action);
  void clearRefs();
  const Frame* frameAt(size_t i) const;
  ptrdiff_t pcOf(size_t i) const;
  uint32_t refFor(Obj* o);
  DebugVar makeVar(const std::string& name, Slot v, uint8_t vt);
  std::vector<DebugVar> scopeVars(const Ref& r);
  std::vector<DebugVar> objectVars(Obj* o, size_t start, size_t count);
};

}  // namespace e7
