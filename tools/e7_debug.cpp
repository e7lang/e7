// `e7 debug` — a small gdb-like command-line debugger built on the C debug API (include/e7/e7.h).
//
//   e7 debug [--break file:line]... [--call fn] [--budget n] [--mem bytes] [--seed n] script.ts|.e7|.e7b
//
// Without --break the script stops at its first statement. Commands (read from stdin):
//   b [file:]line   set a breakpoint          delete [n]   delete one / all breakpoints
//   c  continue     n  next (step over)      s  step (into)     finish  step out
//   bt backtrace    up / down / frame n      locals    globals   p <expr>   list    q quit
// An empty line repeats the previous command.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "common.h"
#include "compiler/compiler.h"
#include "core/module.h"
#include "diag.h"
#include "e7/e7.h"

namespace {

struct Session {
  std::string entryDir;
  std::map<std::string, std::vector<std::string>> sources;
  std::string lastCmd;
  size_t frame = 0;  // selected frame
  bool quit = false;
  bool eof = false;
};

std::string dirOf(const std::string& p) {
  size_t s = p.find_last_of("/\\");
  return s == std::string::npos ? "" : p.substr(0, s + 1);
}

const std::vector<std::string>* sourceOf(Session& s, const std::string& file) {
  auto it = s.sources.find(file);
  if (it != s.sources.end()) return it->second.empty() ? nullptr : &it->second;
  std::vector<std::string> lines;
  std::string text;
  if (readAll(s.entryDir + file, text) || readAll(file, text)) {
    std::string cur;
    for (char c : text) {
      if (c == '\n') { lines.push_back(cur); cur.clear(); }
      else if (c != '\r') cur += c;
    }
    lines.push_back(cur);
  }
  auto& slot = s.sources[file];
  slot = lines;
  return slot.empty() ? nullptr : &slot;
}

void printLine(Session& s, const std::string& file, uint32_t line, bool marker) {
  const auto* src = sourceOf(s, file);
  if (!src || line == 0 || line > src->size()) return;
  printf("%s%4u | %s\n", marker ? "=> " : "   ", line, (*src)[line - 1].c_str());
}

std::string where(const e7_debug_frame& f) {
  std::string w = std::string(f.function) + " (" + f.file + ":" + std::to_string(f.line);
  if (f.column) w += ":" + std::to_string(f.column);
  return w + ")";
}

void printVars(e7_vm* vm, uint32_t ref, const char* indent) {
  const e7_debug_variable* vars;
  size_t n = e7_debug_variables(vm, ref, 0, 0, &vars);
  for (size_t i = 0; i < n; i++) printf("%s%s = %s\n", indent, vars[i].name, vars[i].value);
  if (!n) printf("%s(none)\n", indent);
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

void setBreak(e7_vm* vm, Session& s, const std::string& spec, const std::string& curFile) {
  std::string file = curFile;
  std::string ln = spec;
  size_t colon = spec.rfind(':');
  if (colon != std::string::npos) {
    file = spec.substr(0, colon);
    ln = spec.substr(colon + 1);
  }
  char* end = nullptr;
  long line = strtol(ln.c_str(), &end, 10);
  if (file.empty() || ln.empty() || *end || line <= 0) {
    printf("usage: b [file:]line\n");
    return;
  }
  int id = e7_debug_set_breakpoint(vm, file.c_str(), (uint32_t)line);
  int verified = 0;
  uint32_t actual = 0;
  e7_debug_breakpoint_info(vm, id, &verified, &actual);
  if (verified) printf("Breakpoint %d at %s:%u\n", id, file.c_str(), actual);
  else printf("Breakpoint %d at %s:%ld (pending)\n", id, file.c_str(), line);
  (void)s;
}

e7_debug_action onStop(e7_vm* vm, const e7_debug_stop* st, void* user) {
  Session& s = *static_cast<Session*>(user);
  if (s.eof || s.quit) return E7_DEBUG_CONTINUE;
  s.frame = 0;
  const e7_debug_frame& w = st->where;
  switch (st->reason) {
    case E7_STOP_BREAKPOINT: {
      std::string ids;
      for (size_t i = 0; i < st->num_breakpoints; i++) ids += (i ? "," : "") + std::to_string(st->breakpoints[i]);
      printf("Breakpoint %s, %s\n", ids.c_str(), where(w).c_str());
      break;
    }
    case E7_STOP_DEBUGGER: printf("debugger statement, %s\n", where(w).c_str()); break;
    case E7_STOP_PAUSE: printf("Paused, %s\n", where(w).c_str()); break;
    case E7_STOP_ENTRY: printf("Stopped at entry, %s\n", where(w).c_str()); break;
    case E7_STOP_EXCEPTION: printf("Runtime error: %s\n  at %s\n", st->message, where(w).c_str()); break;
    default: printf("%s\n", where(w).c_str()); break;
  }
  printLine(s, w.file, w.line, false);
  for (;;) {
    printf("(e7db) ");
    fflush(stdout);
    std::string line;
    if (!std::getline(std::cin, line)) {
      printf("\n");
      s.eof = true;  // no more commands: run to the end
      return E7_DEBUG_CONTINUE;
    }
    line = trim(line);
    if (line.empty()) line = s.lastCmd;
    if (line.empty()) continue;
    s.lastCmd = line;
    std::string cmd = line, arg;
    size_t sp = line.find(' ');
    if (sp != std::string::npos) {
      cmd = line.substr(0, sp);
      arg = trim(line.substr(sp + 1));
    }
    const e7_debug_frame* frames;
    size_t nframes = e7_debug_stack(vm, &frames);
    if (s.frame >= nframes) s.frame = 0;
    const e7_debug_frame* cur = nframes ? &frames[s.frame] : nullptr;
    if (cmd == "c" || cmd == "continue") return E7_DEBUG_CONTINUE;
    if (cmd == "n" || cmd == "next") return E7_DEBUG_STEP_OVER;
    if (cmd == "s" || cmd == "step") return E7_DEBUG_STEP_IN;
    if (cmd == "finish" || cmd == "fin") return E7_DEBUG_STEP_OUT;
    if (cmd == "q" || cmd == "quit") {
      s.quit = true;
      return E7_DEBUG_ABORT;
    }
    if (cmd == "b" || cmd == "break") {
      if (arg.empty()) printf("usage: b [file:]line\n");
      else setBreak(vm, s, arg, cur ? cur->file : "");
    } else if (cmd == "delete" || cmd == "d") {
      if (arg.empty()) {
        e7_debug_clear_breakpoints(vm);
        printf("Deleted all breakpoints\n");
      } else if (e7_debug_clear_breakpoint(vm, atoi(arg.c_str())) == E7_OK) {
        printf("Deleted breakpoint %s\n", arg.c_str());
      } else {
        printf("No breakpoint %s\n", arg.c_str());
      }
    } else if (cmd == "bt" || cmd == "backtrace" || cmd == "where") {
      for (size_t i = 0; i < nframes; i++) printf("%s#%zu %s\n", i == s.frame ? "*" : " ", i, where(frames[i]).c_str());
    } else if (cmd == "up" || cmd == "down" || cmd == "frame" || cmd == "f") {
      size_t f = s.frame;
      if (cmd == "up" && f + 1 < nframes) f++;
      else if (cmd == "down" && f > 0) f--;
      else if (cmd == "frame" || cmd == "f") f = arg.empty() ? f : (size_t)atoi(arg.c_str());
      if (f >= nframes) {
        printf("No frame %zu\n", f);
      } else {
        s.frame = f;
        printf("#%zu %s\n", f, where(frames[f]).c_str());
        printLine(s, frames[f].file, frames[f].line, false);
      }
    } else if (cmd == "locals" || cmd == "globals") {
      const e7_debug_scope* scopes;
      size_t n = e7_debug_scopes(vm, s.frame, &scopes);
      std::vector<std::pair<std::string, uint32_t>> sc;
      for (size_t i = 0; i < n; i++) sc.push_back({scopes[i].name, scopes[i].ref});
      bool any = false;
      for (auto& x : sc) {
        bool isGlobals = x.first == "Globals";
        if ((cmd == "globals") != isGlobals) continue;
        any = true;
        if (cmd == "locals" && x.first != "Locals") printf("%s:\n", x.first.c_str());
        printVars(vm, x.second, cmd == "locals" && x.first != "Locals" ? "  " : "");
      }
      if (!any) printf("(none)\n");
    } else if (cmd == "p" || cmd == "print") {
      const e7_debug_variable* r;
      if (arg.empty()) printf("usage: p <expression>\n");
      else if (e7_debug_evaluate(vm, s.frame, arg.c_str(), &r) == E7_OK) printf("%s = %s\n", arg.c_str(), r->value);
      else printf("error: %s\n", e7_last_error(vm));
    } else if (cmd == "list" || cmd == "l") {
      if (cur) {
        uint32_t from = cur->line > 5 ? cur->line - 5 : 1;
        for (uint32_t l = from; l <= cur->line + 5; l++) printLine(s, cur->file, l, l == cur->line);
      }
    } else if (cmd == "help" || cmd == "h") {
      printf(
          "b [file:]line  breakpoint      delete [n]  delete breakpoint(s)\n"
          "c  continue    n  next    s  step    finish  step out\n"
          "bt  backtrace  up/down/frame n  select frame\n"
          "locals  globals  p <name>[.field|[i]]  list  q  quit\n");
    } else {
      printf("unknown command '%s' (try help)\n", cmd.c_str());
    }
  }
}

}  // namespace

int runDebugCommand(int argc, char** argv) {
  std::vector<std::string> breaks;
  std::string file, call;
  int64_t budget = 0;
  size_t mem = 0;
  uint64_t seed = 0;
  bool hasSeed = false;
  for (int i = 0; i < argc; i++) {
    std::string a = argv[i];
    if ((a == "--break" || a == "-b") && i + 1 < argc) breaks.push_back(argv[++i]);
    else if (a == "--call" && i + 1 < argc) call = argv[++i];
    else if (a == "--budget" && i + 1 < argc) budget = atoll(argv[++i]);
    else if (a == "--mem" && i + 1 < argc) mem = (size_t)atoll(argv[++i]);
    else if (a == "--seed" && i + 1 < argc) { seed = strtoull(argv[++i], nullptr, 10); hasSeed = true; }
    else if (a.size() && a[0] == '-') {
      fprintf(stderr, "usage: e7 debug [--break file:line]... [--call fn] [--budget n] [--mem bytes] [--seed n] <file>\n");
      return 2;
    } else file = a;
  }
  if (file.empty()) {
    fprintf(stderr, "usage: e7 debug [--break file:line]... [--call fn] [--budget n] [--mem bytes] [--seed n] <file>\n");
    return 2;
  }
  std::vector<uint8_t> bytes;
  if (endsWith(file, ".e7b")) {
    std::string data;
    if (!readAll(file, data)) { fprintf(stderr, "e7: cannot read %s\n", file.c_str()); return 1; }
    bytes.assign(data.begin(), data.end());
  } else {
    e7::CompileOptions copts;
    copts.debugInfo = true;
    e7::CompileOutput co = e7::compileProgram(file, e7::readFileFromDisk, copts);
    if (!co.ok) {
      fprintf(stderr, "%s", formatCompileErrors(co, true).c_str());
      return 1;
    }
    bytes = e7::serializeModule(co.module);
  }
  e7_config cfg;
  e7_config_init(&cfg);
  if (mem) cfg.memory_limit = mem;
  e7_vm* vm = e7_vm_new(&cfg);
  if (!vm) { fprintf(stderr, "e7: cannot create VM\n"); return 1; }
  if (hasSeed) e7_seed_random(vm, seed);
  if (budget) e7_set_budget(vm, budget);
  e7_set_print(vm, [](void*, const char* t, size_t n) { printf("%.*s\n", (int)n, t); fflush(stdout); }, nullptr);
  Session s;
  s.entryDir = dirOf(file);
  e7_debug_attach(vm, onStop, &s);
  for (auto& b : breaks) setBreak(vm, s, b, "");
  if (breaks.empty()) e7_debug_stop_on_entry(vm);
  e7_module* mod = nullptr;
  e7_status st = e7_load(vm, bytes.data(), bytes.size(), &mod);
  if (st == E7_OK) {
    if (call.empty() && e7_has_function(mod, "main")) call = "main";
    if (!call.empty()) {
      e7_value ret;
      st = e7_call(vm, mod, call.c_str(), nullptr, 0, &ret);
      if (st == E7_OK && ret.type != E7_VOID) {
        switch (ret.type) {
          case E7_NUMBER: printf("[%s returned %.17g]\n", call.c_str(), ret.v.num); break;
          case E7_BOOL: printf("[%s returned %s]\n", call.c_str(), ret.v.boolean ? "true" : "false"); break;
          case E7_INT: printf("[%s returned %lld]\n", call.c_str(), (long long)ret.v.i); break;
          case E7_UINT: printf("[%s returned %llu]\n", call.c_str(), (unsigned long long)ret.v.u); break;
          case E7_STRING: printf("[%s returned %.*s]\n", call.c_str(), (int)ret.v.str.len, ret.v.str.ptr); break;
          default: break;
        }
      }
    }
  }
  int rc = 0;
  if (s.quit) {
    printf("[quit]\n");
  } else if (st != E7_OK) {
    fflush(stdout);
    fprintf(stderr, "%s\n", e7_last_error(vm));
    rc = st == E7_ERR_RUNTIME ? 3 : 1;
  } else {
    printf("[program exited]\n");
  }
  e7_vm_free(vm);
  return rc;
}
