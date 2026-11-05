// Debugger tests: the C debug API (both execution models), debug information in bytecode
// (serialization, backward compatibility, verifier rejection of malformed debug sections) and
// columns in stack traces.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "compiler/compiler.h"
#include "core/module.h"
#include "e7/e7.h"

static int failures = 0;
#define CHECK(cond)                                                             \
  do {                                                                          \
    if (!(cond)) {                                                              \
      fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
      failures++;                                                               \
    }                                                                           \
  } while (0)
#define CHECK_EQ_STR(a, b)                                                                              \
  do {                                                                                                  \
    std::string a_ = (a), b_ = (b);                                                                     \
    if (a_ != b_) {                                                                                     \
      fprintf(stderr, "CHECK failed %s:%d: %s == %s\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, #a, #b, \
              a_.c_str(), b_.c_str());                                                                  \
      failures++;                                                                                       \
    }                                                                                                   \
  } while (0)

// ------------------------------------------------------------------ test program
static const char* kMain =
    "import { scale, Counter } from \"./lib/util\";\n"          // 1
    "class Point {\n"                                             // 2
    "  constructor(public x: number, public y: number) {}\n"      // 3
    "  norm(): number {\n"                                        // 4
    "    const sq = this.x * this.x + this.y * this.y;\n"         // 5
    "    return Math.sqrt(sq);\n"                                 // 6
    "  }\n"                                                       // 7
    "}\n"                                                         // 8
    "let total = 0;\n"                                            // 9
    "const names: string[] = [\"a\", \"b\"];\n"                   // 10
    "function add(a: number, b: number): number {\n"              // 11
    "  const s = a + b;\n"                                        // 12
    "  total = total + s;\n"                                      // 13
    "  return s;\n"                                               // 14
    "}\n"                                                         // 15
    "function makeCounter(start: i32): () => i32 {\n"             // 16
    "  let n: i32 = start;\n"                                     // 17
    "  return (): i32 => {\n"                                     // 18
    "    n = n + 1;\n"                                            // 19
    "    return n;\n"                                             // 20
    "  };\n"                                                      // 21
    "}\n"                                                         // 22
    "export function main(): number {\n"                          // 23
    "  const p = new Point(3, 4);\n"                              // 24
    "  const flag = true;\n"                                      // 25
    "  const big = 12345678901234567890n;\n"                      // 26
    "  const m = new Map<string, number>();\n"                    // 27
    "  m.set(\"k1\", 1);\n"                                       // 28
    "  const st = new Set<number>([7, 8]);\n"                     // 29
    "  const arr = [[1, 2], [3]];\n"                              // 30
    "  const rec = { name: \"r\", inner: { v: 1 } };\n"           // 31
    "  const tup: [number, string] = [1, \"t\"];\n"               // 32
    "  const bytes = new Uint8Array(3);\n"                        // 33
    "  const nothing: string | null = null;\n"                    // 34
    "  const c = makeCounter(10);\n"                              // 35
    "  c();\n"                                                    // 36
    "  let r = add(1, 2);\n"                                      // 37
    "  r = add(r, p.norm());\n"                                   // 38
    "  debugger;\n"                                               // 39
    "  r = r + scale(2) + new Counter().count;\n"                 // 40
    "  return r;\n"                                               // 41
    "}\n"                                                         // 42
    "add(5, 6);\n"                                                // 43
    "export function fail(): number {\n"                          // 44
    "  const xs: number[] = [1];\n"                               // 45
    "  return xs[5];\n"                                           // 46
    "}\n"                                                         // 47
    "export function sorted(): number {\n"                        // 48
    "  const v = [3, 1, 2].sort((x: number, y: number): number => {\n"  // 49
    "    return x - y;\n"                                         // 50
    "  });\n"                                                     // 51
    "  return v[0];\n"                                            // 52
    "}\n"                                                         // 53
    "export function spin(): number {\n"                          // 54
    "  let i = 0;\n"                                              // 55
    "  while (i >= 0) {\n"                                        // 56
    "    i = i + 1;\n"                                            // 57
    "  }\n"                                                       // 58
    "  return i;\n"                                               // 59
    "}\n";                                                        // 60

static const char* kUtil =
    "export function scale(x: number): number {\n"  // 1
    "  const k = 10;\n"                              // 2
    "  return x * k;\n"                              // 3
    "}\n"                                            // 4
    "export class Counter { count = 0; }\n";         // 5

static std::map<std::string, std::string> files = {{"lib/util.ts", kUtil}};

static int readFile(void*, const char* path, char** data, size_t* len) {
  auto it = files.find(path);
  if (it == files.end()) return 1;
  *data = (char*)malloc(it->second.size() + 1);
  memcpy(*data, it->second.data(), it->second.size());
  *len = it->second.size();
  return 0;
}

static std::vector<uint8_t> compileProgram(const char* src, bool debug, const char* name = "main.ts") {
  e7_compile_options o;
  e7_compile_options_init(&o);
  o.flags = debug ? E7_COMPILE_DEBUG : 0;
  o.read_file = readFile;
  uint8_t* out = nullptr;
  size_t len = 0;
  char* errs = nullptr;
  e7_status st = e7_compile_ex(src, strlen(src), name, &o, &out, &len, &errs);
  if (st != E7_OK) {
    fprintf(stderr, "compile failed: %s\n", errs ? errs : "?");
    e7_free(errs);
    failures++;
    return {};
  }
  std::vector<uint8_t> v(out, out + len);
  e7_free(out);
  return v;
}

// ------------------------------------------------------------------ helpers
struct Var {
  std::string name, type, value;
  uint32_t ref;
  size_t count;
};

static std::vector<Var> vars(e7_vm* vm, uint32_t ref, size_t start = 0, size_t count = 0) {
  const e7_debug_variable* v;
  size_t n = e7_debug_variables(vm, ref, start, count, &v);
  std::vector<Var> out;
  for (size_t i = 0; i < n; i++) out.push_back(Var{v[i].name, v[i].type, v[i].value, v[i].ref, v[i].count});
  return out;
}

static uint32_t scopeRef(e7_vm* vm, size_t frame, const char* name) {
  const e7_debug_scope* s;
  size_t n = e7_debug_scopes(vm, frame, &s);
  for (size_t i = 0; i < n; i++)
    if (strcmp(s[i].name, name) == 0) return s[i].ref;
  return 0;
}

static const Var* find(const std::vector<Var>& vs, const std::string& name) {
  for (auto& v : vs)
    if (v.name == name) return &v;
  return nullptr;
}

static std::string varsText(const std::vector<Var>& vs) {
  std::string s;
  for (auto& v : vs) s += v.name + ":" + v.type + "=" + v.value + ";";
  return s;
}

static std::string evalText(e7_vm* vm, size_t frame, const char* expr) {
  const e7_debug_variable* r;
  if (e7_debug_evaluate(vm, frame, expr, &r) != E7_OK) return std::string("ERR ") + e7_last_error(vm);
  return std::string(r->type) + " " + r->value;
}

static std::string stackText(e7_vm* vm) {
  const e7_debug_frame* f;
  size_t n = e7_debug_stack(vm, &f);
  std::string s;
  for (size_t i = 0; i < n; i++) {
    s += std::string(f[i].function) + "@" + f[i].file + ":" + std::to_string(f[i].line);
    if (f[i].column) s += ":" + std::to_string(f[i].column);
    s += i + 1 < n ? " " : "";
  }
  return s;
}

static std::string stopText(const e7_debug_stop* st) {
  static const char* names[] = {"?", "breakpoint", "step", "debugger", "pause", "exception", "entry"};
  std::string s = names[st->reason];
  s += " " + std::string(st->where.function) + "@" + st->where.file + ":" + std::to_string(st->where.line);
  return s;
}

// Synchronous handler driven by a script of actions; records every stop.
struct Driver {
  std::vector<std::string> stops;
  std::vector<e7_debug_action> actions;  // consumed in order; CONTINUE when exhausted
  std::function<void(e7_vm*, const e7_debug_stop*, size_t index)> inspect;
  size_t next = 0;
};

static e7_debug_action driverFn(e7_vm* vm, const e7_debug_stop* st, void* user) {
  Driver& d = *static_cast<Driver*>(user);
  d.stops.push_back(stopText(st));
  if (d.inspect) d.inspect(vm, st, d.stops.size() - 1);
  return d.next < d.actions.size() ? d.actions[d.next++] : E7_DEBUG_CONTINUE;
}

static e7_vm* newVm() {
  e7_vm* vm = e7_vm_new(nullptr);
  e7_set_print(vm, [](void*, const char*, size_t) {}, nullptr);
  return vm;
}

// ------------------------------------------------------------------ tests
static void testBreakpointsAndStepping() {
  std::vector<uint8_t> bc = compileProgram(kMain, true);
  e7_vm* vm = newVm();
  Driver d;
  CHECK(e7_debug_attach(vm, driverFn, &d) == E7_OK);
  int bp = e7_debug_set_breakpoint(vm, "main.ts", 12);  // pending: nothing loaded yet
  CHECK(bp > 0);
  int verified = -1;
  uint32_t line = 0;
  CHECK(e7_debug_breakpoint_info(vm, bp, &verified, &line) == 1 && verified == 0);
  d.inspect = [&](e7_vm* vm, const e7_debug_stop* st, size_t i) {
    if (i == 0) {  // add(5, 6) from top-level code
      CHECK(st->num_breakpoints == 1 && st->breakpoints[0] == bp);
      CHECK(st->where.column == 3);
      CHECK_EQ_STR(stackText(vm), "add@main.ts:12:3 <init>@main.ts:43:1");
      auto loc = vars(vm, scopeRef(vm, 0, "Locals"));
      CHECK(find(loc, "a") && find(loc, "a")->value == "5" && find(loc, "a")->type == "number");
      CHECK(find(loc, "b") && find(loc, "b")->value == "6");
      CHECK(find(loc, "s") != nullptr);
      auto glob = vars(vm, scopeRef(vm, 0, "Globals"));
      CHECK(find(glob, "total") && find(glob, "names"));
    }
  };
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_OK);
  CHECK(e7_debug_breakpoint_info(vm, bp, &verified, &line) == 1 && verified == 1 && line == 12);
  CHECK(d.stops.size() == 1 && d.stops[0] == "breakpoint add@main.ts:12");

  // main: bp in add (line 37), step over x3, returns into main mid-statement, then next statement
  d.stops.clear();
  d.inspect = nullptr;
  d.next = 0;
  d.actions = {E7_DEBUG_STEP_OVER, E7_DEBUG_STEP_OVER,
               // `return s;`: back in main, where the call result already is in `r`: next statement
               E7_DEBUG_STEP_OVER,
               // line 38: step in -> p.norm() is called first
               E7_DEBUG_STEP_IN, E7_DEBUG_STEP_IN,
               // in norm (line 6): step out -> back in main mid-statement (before calling add)
               E7_DEBUG_STEP_OUT,
               // step in -> add (the breakpoint there is reported)
               E7_DEBUG_STEP_IN,
               // continue -> debugger statement
               E7_DEBUG_CONTINUE, E7_DEBUG_CONTINUE};
  e7_value ret;
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_OK);
  CHECK(ret.type == E7_NUMBER && ret.v.num == 28);
  std::vector<std::string> want = {
      "breakpoint add@main.ts:12", "step add@main.ts:13",       "step add@main.ts:14",
      "step main@main.ts:38",      "step Point.norm@main.ts:5", "step Point.norm@main.ts:6",
      "step main@main.ts:38",      "breakpoint add@main.ts:12", "debugger main@main.ts:39"};
  CHECK(d.stops == want);
  if (d.stops != want)
    for (auto& s : d.stops) fprintf(stderr, "  stop: %s\n", s.c_str());

  // clearing the breakpoint: only the debugger statement stops
  CHECK(e7_debug_clear_breakpoint(vm, bp) == E7_OK);
  CHECK(e7_debug_clear_breakpoint(vm, bp) == E7_ERR_NOT_FOUND);
  CHECK(e7_debug_breakpoint_info(vm, bp, nullptr, nullptr) == 0);
  d.stops.clear();
  d.actions.clear();
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_OK);
  CHECK(d.stops.size() == 1 && d.stops[0] == "debugger main@main.ts:39");
  e7_vm_free(vm);
}

static void testVariables() {
  std::vector<uint8_t> bc = compileProgram(kMain, true);
  e7_vm* vm = newVm();
  Driver d;
  e7_debug_attach(vm, driverFn, &d);
  bool checked = false;
  d.inspect = [&](e7_vm* vm, const e7_debug_stop* st, size_t) {
    if (st->reason != E7_STOP_DEBUGGER) return;
    checked = true;
    auto loc = vars(vm, scopeRef(vm, 0, "Locals"));
    CHECK_EQ_STR(varsText(loc),
                 "p:Point=Point { x: 3, y: 4 };flag:boolean=true;big:bigint=12345678901234567890n;"
                 "m:Map=Map(1) { 'k1' => 1 };st:Set=Set(2) { 7, 8 };arr:Array=[ [Array], [Array] ];"
                 "rec:Object={ name: 'r', inner: [Object] };tup:tuple=[ 1, 't' ];"
                 "bytes:Uint8Array=Uint8Array(3) [ 0, 0, 0 ];nothing:null=null;"
                 "c:function=[Function: <anonymous>];r:number=8;");
    // expand: class instance
    const Var* p = find(loc, "p");
    CHECK(p && p->ref && p->count == 2);
    CHECK_EQ_STR(varsText(vars(vm, p->ref)), "x:number=3;y:number=4;");
    // nested arrays, with paging
    const Var* arr = find(loc, "arr");
    auto rows = vars(vm, arr->ref);
    CHECK(rows.size() == 2 && rows[0].count == 2 && rows[0].ref);
    CHECK_EQ_STR(varsText(vars(vm, rows[0].ref)), "0:number=1;1:number=2;");
    CHECK_EQ_STR(varsText(vars(vm, rows[0].ref, 1, 5)), "1:number=2;");
    CHECK(vars(vm, rows[0].ref, 7, 1).empty());
    // the same object keeps its reference while paused
    CHECK(vars(vm, arr->ref)[0].ref == rows[0].ref);
    // nested record
    auto rec = vars(vm, find(loc, "rec")->ref);
    CHECK_EQ_STR(varsText(rec), "name:string='r';inner:Object={ v: 1 };");
    CHECK_EQ_STR(varsText(vars(vm, rec[1].ref)), "v:number=1;");
    // Map / Set entries, tuple, bytes
    CHECK_EQ_STR(varsText(vars(vm, find(loc, "m")->ref)), "'k1':number=1;");
    CHECK_EQ_STR(varsText(vars(vm, find(loc, "st")->ref)), "[0]:number=7;[1]:number=8;");
    CHECK_EQ_STR(varsText(vars(vm, find(loc, "tup")->ref)), "0:number=1;1:string='t';");
    CHECK_EQ_STR(varsText(vars(vm, find(loc, "bytes")->ref, 0, 1)), "0:u8=0;");
    // closure: captured variable of the counter (i32)
    CHECK_EQ_STR(varsText(vars(vm, find(loc, "c")->ref)), "n:i32=11;");
    // globals
    auto glob = vars(vm, scopeRef(vm, 0, "Globals"));
    CHECK_EQ_STR(varsText(glob), "total:number=22;names:Array=[ 'a', 'b' ];");
    // no closure scope in main
    CHECK(scopeRef(vm, 0, "Closure") == 0);
    // evaluate
    CHECK_EQ_STR(evalText(vm, 0, "p.x"), "number 3");
    CHECK_EQ_STR(evalText(vm, 0, "arr[0][1]"), "number 2");
    CHECK_EQ_STR(evalText(vm, 0, "arr[1]"), "Array [ 3 ]");
    CHECK_EQ_STR(evalText(vm, 0, "rec.inner.v"), "number 1");
    CHECK_EQ_STR(evalText(vm, 0, "rec['name']"), "string 'r'");
    CHECK_EQ_STR(evalText(vm, 0, "m['k1']"), "number 1");
    CHECK_EQ_STR(evalText(vm, 0, "m.size"), "number 1");
    CHECK_EQ_STR(evalText(vm, 0, "names.length"), "number 2");
    CHECK_EQ_STR(evalText(vm, 0, "names[1].length"), "number 1");
    CHECK_EQ_STR(evalText(vm, 0, "tup[1]"), "string 't'");
    CHECK_EQ_STR(evalText(vm, 0, "total"), "number 22");
    CHECK(evalText(vm, 0, "nope").rfind("ERR no variable named 'nope'", 0) == 0);
    CHECK(evalText(vm, 0, "arr[9]").rfind("ERR cannot read '9'", 0) == 0);
    CHECK(evalText(vm, 0, "p.").rfind("ERR", 0) == 0);
    CHECK(evalText(vm, 0, "p[").rfind("ERR", 0) == 0);
    CHECK(evalText(vm, 5, "p").rfind("ERR", 0) == 0);  // no such frame
    // invalid references
    CHECK(vars(vm, 0).empty() && vars(vm, 99999).empty());
  };
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_OK);
  e7_value ret;
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_OK);
  CHECK(checked);
  // not paused: inspection returns nothing
  CHECK(e7_debug_stack(vm, nullptr) == 0);
  CHECK(e7_debug_is_paused(vm) == 0);
  e7_debug_stop info;
  CHECK(e7_debug_stop_info(vm, &info) == E7_ERR_STATE);
  e7_vm_free(vm);
}

static void testClosuresAndImports() {
  std::vector<uint8_t> bc = compileProgram(kMain, true);
  e7_vm* vm = newVm();
  Driver d;
  e7_debug_attach(vm, driverFn, &d);
  e7_debug_set_breakpoint(vm, "main.ts", 36);                // c();
  int bu = e7_debug_set_breakpoint(vm, "/abs/path/lib/util.ts", 3);  // path suffix match
  std::vector<std::string> upv;
  d.inspect = [&](e7_vm* vm, const e7_debug_stop* st, size_t i) {
    if (i == 1) {  // inside the arrow function: captured n
      CHECK(st->where.line == 19);
      upv.push_back(varsText(vars(vm, scopeRef(vm, 0, "Closure"))));
      CHECK_EQ_STR(evalText(vm, 0, "n"), "i32 10");
      CHECK_EQ_STR(stackText(vm), "makeCounter.<anonymous>@main.ts:19:5 main@main.ts:36:3");
      // frames below: main's locals are visible from frame 1
      CHECK_EQ_STR(evalText(vm, 1, "flag"), "boolean true");
    }
    if (st->reason == E7_STOP_BREAKPOINT && st->where.line == 3) {
      CHECK_EQ_STR(std::string(st->where.file), "lib/util.ts");
      CHECK(st->num_breakpoints == 1 && st->breakpoints[0] == bu);
      CHECK_EQ_STR(varsText(vars(vm, scopeRef(vm, 0, "Locals"))), "x:number=2;k:number=10;");
      CHECK_EQ_STR(stackText(vm), "scale@lib/util.ts:3:3 main@main.ts:40:11");
    }
  };
  d.actions = {E7_DEBUG_STEP_IN, E7_DEBUG_STEP_OVER, E7_DEBUG_STEP_OVER, E7_DEBUG_STEP_OVER};
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_OK);
  e7_value ret;
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_OK);
  std::vector<std::string> want = {"breakpoint main@main.ts:36", "step makeCounter.<anonymous>@main.ts:19",
                                   "step makeCounter.<anonymous>@main.ts:20", "step main@main.ts:37",
                                   "step main@main.ts:38", "debugger main@main.ts:39", "breakpoint scale@lib/util.ts:3"};
  CHECK(d.stops == want);
  if (d.stops != want)
    for (auto& s : d.stops) fprintf(stderr, "  stop: %s\n", s.c_str());
  CHECK(upv.size() == 1 && upv[0] == "n:i32=10;");
  e7_vm_free(vm);
}

static void testBreakpointResolution() {
  std::vector<uint8_t> bc = compileProgram(kMain, true);
  e7_vm* vm = newVm();
  Driver d;
  e7_debug_attach(vm, driverFn, &d);
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_OK);
  int verified;
  uint32_t line;
  int b1 = e7_debug_set_breakpoint(vm, "main.ts", 1);  // import: moves to the first statement
  CHECK(e7_debug_breakpoint_info(vm, b1, &verified, &line) && verified && line == 5);
  int b2 = e7_debug_set_breakpoint(vm, "main.ts", 15);  // closing brace: next statement
  CHECK(e7_debug_breakpoint_info(vm, b2, &verified, &line) && verified && line == 17);
  int b3 = e7_debug_set_breakpoint(vm, "main.ts", 1000);
  CHECK(e7_debug_breakpoint_info(vm, b3, &verified, &line) && !verified && line == 1000);
  int b4 = e7_debug_set_breakpoint(vm, "other.ts", 3);
  CHECK(e7_debug_breakpoint_info(vm, b4, &verified, nullptr) && !verified);
  int b5 = e7_debug_set_breakpoint(vm, "util.ts", 2);  // a file-name suffix of lib/util.ts
  CHECK(e7_debug_breakpoint_info(vm, b5, &verified, &line) && verified && line == 2);
  e7_debug_clear_breakpoints(vm);
  CHECK(e7_debug_breakpoint_info(vm, b5, nullptr, nullptr) == 0);
  e7_vm_free(vm);
}

static void testStopOnEntryAndExceptions() {
  std::vector<uint8_t> bc = compileProgram(kMain, true);
  e7_vm* vm = newVm();
  Driver d;
  e7_debug_attach(vm, driverFn, &d);
  e7_debug_stop_on_entry(vm);
  std::string msg;
  d.inspect = [&](e7_vm* vm, const e7_debug_stop* st, size_t) {
    if (st->reason == E7_STOP_EXCEPTION) {
      msg = st->message;
      CHECK_EQ_STR(stackText(vm), "fail@main.ts:46:12");
      CHECK_EQ_STR(varsText(vars(vm, scopeRef(vm, 0, "Locals"))), "xs:Array=[ 1 ];");
      e7_debug_stop again;
      CHECK(e7_debug_stop_info(vm, &again) == E7_OK && again.reason == E7_STOP_EXCEPTION);
    }
  };
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_OK);
  CHECK(d.stops.size() == 1 && d.stops[0] == "entry <init>@main.ts:9");
  e7_value ret;
  CHECK(e7_call(vm, mod, "fail", nullptr, 0, &ret) == E7_ERR_RUNTIME);
  CHECK(msg == "index 5 out of range (length 1)");
  CHECK(strstr(e7_last_error(vm), "at fail (main.ts:46:12)") != nullptr);
  // abort from the handler
  d.actions = {E7_DEBUG_ABORT};
  d.next = 0;
  d.stops.clear();
  e7_debug_set_breakpoint(vm, "main.ts", 13);
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_ERR_RUNTIME);
  CHECK(strstr(e7_last_error(vm), "execution aborted by the debugger") != nullptr);
  CHECK(d.stops.size() == 1);  // the abort itself is not reported as an exception
  // break on errors can be turned off
  e7_debug_clear_breakpoints(vm);
  e7_debug_break_on_error(vm, 0);
  d.stops.clear();
  CHECK(e7_call(vm, mod, "fail", nullptr, 0, &ret) == E7_ERR_RUNTIME);
  CHECK(d.stops.empty());
  // the VM is still usable
  e7_debug_break_on_error(vm, 1);
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_OK && ret.v.num == 28);
  e7_vm_free(vm);
}

// try/catch: a caught error is not a stop; the same error uncaught is (sync and pause-to-host)
static void testCaughtErrors() {
  const char* src =
      "function risky(n: number): number {\n"
      "  if (n > 1) throw new RangeError(\"big \" + n);\n"
      "  return n;\n"
      "}\n"
      "export function handled(): number {\n"
      "  let total = 0;\n"
      "  for (let i = 0; i < 4; i++) {\n"
      "    try { total += risky(i); } catch (e) { total += 100; }\n"
      "  }\n"
      "  return total;\n"
      "}\n"
      "export function unhandled(): number { return risky(5); }\n";
  std::vector<uint8_t> bc = compileProgram(src, true);
  e7_vm* vm = newVm();
  Driver d;
  e7_debug_attach(vm, driverFn, &d);
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_OK);
  e7_value ret;
  CHECK(e7_call(vm, mod, "handled", nullptr, 0, &ret) == E7_OK && ret.v.num == 201);
  CHECK(d.stops.empty());
  CHECK(e7_call(vm, mod, "unhandled", nullptr, 0, &ret) == E7_ERR_RUNTIME);
  CHECK(d.stops.size() == 1 && d.stops[0] == "exception risky@main.ts:2");
  e7_vm_free(vm);

  // pause-to-host mode
  vm = newVm();
  e7_debug_attach(vm, nullptr, nullptr);
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_OK);
  CHECK(e7_call(vm, mod, "handled", nullptr, 0, &ret) == E7_OK && ret.v.num == 201);
  CHECK(e7_call(vm, mod, "unhandled", nullptr, 0, &ret) == E7_PAUSED);
  e7_debug_stop st;
  CHECK(e7_debug_stop_info(vm, &st) == E7_OK && st.reason == E7_STOP_EXCEPTION);
  CHECK(e7_debug_resume(vm, E7_DEBUG_CONTINUE, &ret) == E7_ERR_RUNTIME);
  e7_vm_free(vm);
}

static void testSortCallbackSync() {
  std::vector<uint8_t> bc = compileProgram(kMain, true);
  e7_vm* vm = newVm();
  Driver d;
  e7_debug_attach(vm, driverFn, &d);
  e7_debug_set_breakpoint(vm, "main.ts", 50);
  bool sawNative = false;
  d.inspect = [&](e7_vm* vm, const e7_debug_stop*, size_t) {
    std::string s = stackText(vm);
    sawNative = sawNative || s.find("sorted@main.ts:49") != std::string::npos;
  };
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_OK);
  e7_value ret;
  CHECK(e7_call(vm, mod, "sorted", nullptr, 0, &ret) == E7_OK && ret.v.num == 1);
  CHECK(d.stops.size() >= 2);
  CHECK(sawNative);
  e7_vm_free(vm);
}

static void testPauseFromThread() {
  std::vector<uint8_t> bc = compileProgram(kMain, true);
  e7_vm* vm = newVm();
  Driver d;
  e7_debug_attach(vm, driverFn, &d);
  std::string where;
  d.inspect = [&](e7_vm* vm, const e7_debug_stop* st, size_t) {
    where = stopText(st);
    CHECK(evalText(vm, 0, "i").rfind("number ", 0) == 0);
  };
  d.actions = {E7_DEBUG_ABORT};
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_OK);
  std::atomic<bool> done{false};
  std::thread t([&] {
    while (!done.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      e7_debug_pause(vm);
    }
  });
  e7_value ret;
  e7_status st = e7_call(vm, mod, "spin", nullptr, 0, &ret);
  done = true;
  t.join();
  CHECK(st == E7_ERR_RUNTIME);
  CHECK(where == "pause spin@main.ts:57" || where == "pause spin@main.ts:56");
  e7_vm_free(vm);
}

// pause-to-host model: e7_load / e7_call return E7_PAUSED
static void testHostMode() {
  std::vector<uint8_t> bc = compileProgram(kMain, true);
  e7_vm* vm = newVm();
  CHECK(e7_debug_attach(vm, nullptr, nullptr) == E7_OK);
  e7_debug_set_breakpoint(vm, "main.ts", 12);
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_PAUSED);
  CHECK(mod != nullptr);
  CHECK(e7_debug_is_paused(vm));
  e7_debug_stop st;
  CHECK(e7_debug_stop_info(vm, &st) == E7_OK && st.reason == E7_STOP_BREAKPOINT && st.where.line == 12);
  CHECK_EQ_STR(stackText(vm), "add@main.ts:12:3 <init>@main.ts:43:1");
  CHECK_EQ_STR(evalText(vm, 0, "a"), "number 5");
  // calls are refused while paused; attaching/detaching too
  e7_value ret;
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_ERR_STATE);
  CHECK(e7_debug_detach(vm) == E7_ERR_STATE);
  CHECK(e7_debug_resume(vm, E7_DEBUG_STEP_OVER, &ret) == E7_PAUSED);
  CHECK(e7_debug_stop_info(vm, &st) == E7_OK && st.reason == E7_STOP_STEP && st.where.line == 13);
  CHECK(e7_debug_resume(vm, E7_DEBUG_CONTINUE, &ret) == E7_OK);  // top-level code finished
  CHECK(!e7_debug_is_paused(vm));
  CHECK(e7_debug_resume(vm, E7_DEBUG_CONTINUE, &ret) == E7_ERR_STATE);
  // main: two stops in add, then the debugger statement
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_PAUSED);
  CHECK(e7_debug_stop_info(vm, &st) == E7_OK && st.where.line == 12);
  CHECK_EQ_STR(stackText(vm), "add@main.ts:12:3 main@main.ts:37:11");
  CHECK(e7_debug_resume(vm, E7_DEBUG_STEP_OUT, &ret) == E7_PAUSED);
  CHECK(e7_debug_stop_info(vm, &st) == E7_OK && st.reason == E7_STOP_STEP && st.where.line == 38);
  CHECK(e7_debug_resume(vm, E7_DEBUG_CONTINUE, &ret) == E7_PAUSED);
  CHECK(e7_debug_stop_info(vm, &st) == E7_OK && st.where.line == 12);
  CHECK(e7_debug_resume(vm, E7_DEBUG_CONTINUE, &ret) == E7_PAUSED);
  CHECK(e7_debug_stop_info(vm, &st) == E7_OK && st.reason == E7_STOP_DEBUGGER && st.where.line == 39);
  CHECK(e7_resume(vm, &ret) == E7_OK);  // e7_resume continues too
  CHECK(ret.type == E7_NUMBER && ret.v.num == 28);
  // runtime error: reported first, with the frames, then returned
  CHECK(e7_call(vm, mod, "fail", nullptr, 0, &ret) == E7_PAUSED);
  CHECK(e7_debug_stop_info(vm, &st) == E7_OK && st.reason == E7_STOP_EXCEPTION);
  CHECK_EQ_STR(std::string(st.message), "index 5 out of range (length 1)");
  CHECK_EQ_STR(varsText(vars(vm, scopeRef(vm, 0, "Locals"))), "xs:Array=[ 1 ];");
  CHECK(e7_debug_resume(vm, E7_DEBUG_STEP_OVER, &ret) == E7_ERR_RUNTIME);
  CHECK(strstr(e7_last_error(vm), "index 5 out of range") && strstr(e7_last_error(vm), "at fail (main.ts:46:12)"));
  // abort
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_PAUSED);
  CHECK(e7_debug_resume(vm, E7_DEBUG_ABORT, &ret) == E7_ERR_RUNTIME);
  CHECK(!e7_debug_is_paused(vm));
  // sort comparator breakpoints are skipped in this model (a native is on the C++ stack)
  e7_debug_clear_breakpoints(vm);
  e7_debug_set_breakpoint(vm, "main.ts", 50);
  CHECK(e7_call(vm, mod, "sorted", nullptr, 0, &ret) == E7_OK && ret.v.num == 1);
  // e7_abort_suspended also ends a pause
  e7_debug_clear_breakpoints(vm);
  e7_debug_pause(vm);
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_PAUSED);
  CHECK(e7_debug_stop_info(vm, &st) == E7_OK && st.reason == E7_STOP_PAUSE && st.where.line == 24);
  e7_abort_suspended(vm);
  CHECK(!e7_debug_is_paused(vm));
  // detach: back to the normal interpreter
  CHECK(e7_debug_detach(vm) == E7_OK);
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_OK && ret.v.num == 28);
  e7_vm_free(vm);
}

// pause-to-host with stepping across the init / main boundary and budget suspension
static void testHostModeBudget() {
  const char* src =
      "let s = 0;\n"
      "for (let i = 0; i < 100000; i++) {\n"
      "  s = s + i;\n"
      "}\n"
      "export function main(): number {\n"
      "  return s;\n"
      "}\n";
  std::vector<uint8_t> bc = compileProgram(src, true);
  e7_config cfg;
  e7_config_init(&cfg);
  cfg.suspend_on_budget = 1;
  e7_vm* vm = e7_vm_new(&cfg);
  e7_debug_attach(vm, nullptr, nullptr);
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_OK);
  e7_set_budget(vm, 1000);
  e7_value ret;
  e7_status st = e7_call(vm, mod, "main", nullptr, 0, &ret);
  CHECK(st == E7_OK && ret.v.num == 4999950000.0);
  e7_debug_stop_on_entry(vm);
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_PAUSED);
  CHECK(e7_debug_resume(vm, E7_DEBUG_CONTINUE, &ret) == E7_OK && ret.v.num == 4999950000.0);
  e7_vm_free(vm);
}

// modules without debug information: line-granular breakpoints, no columns, no local names
static void testWithoutDebugInfo() {
  std::vector<uint8_t> bc = compileProgram(kMain, false);
  e7_vm* vm = newVm();
  Driver d;
  e7_debug_attach(vm, driverFn, &d);
  e7_debug_set_breakpoint(vm, "main.ts", 13);
  d.inspect = [&](e7_vm* vm, const e7_debug_stop* st, size_t) {
    CHECK(st->where.column == 0);
    CHECK(vars(vm, scopeRef(vm, 0, "Locals")).empty());
    CHECK(find(vars(vm, scopeRef(vm, 0, "Globals")), "total") != nullptr);
  };
  d.actions = {E7_DEBUG_STEP_OVER};
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_OK);
  CHECK(d.stops.size() == 2 && d.stops[0] == "breakpoint add@main.ts:13" && d.stops[1] == "step add@main.ts:14");
  e7_value ret;
  CHECK(e7_call(vm, mod, "fail", nullptr, 0, &ret) == E7_ERR_RUNTIME);
  CHECK(strstr(e7_last_error(vm), "at fail (main.ts:46)") != nullptr && !strstr(e7_last_error(vm), ":46:"));
  e7_vm_free(vm);
}

// no debugger: `debugger;` is a no-op; traces have columns only with debug information
static void testNoDebugger() {
  e7_vm* vm = newVm();
  std::vector<uint8_t> bc = compileProgram(kMain, true);
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, bc.data(), bc.size(), &mod) == E7_OK);
  e7_value ret;
  CHECK(e7_call(vm, mod, "main", nullptr, 0, &ret) == E7_OK && ret.v.num == 28);
  CHECK(e7_call(vm, mod, "fail", nullptr, 0, &ret) == E7_ERR_RUNTIME);
  CHECK_EQ_STR(e7_last_error(vm), "index 5 out of range (length 1)\n    at fail (main.ts:46:12)");
  CHECK(e7_debug_set_breakpoint(vm, "main.ts", 12) == 0);  // no debugger attached
  CHECK(e7_debug_resume(vm, E7_DEBUG_CONTINUE, &ret) == E7_ERR_STATE);
  e7_vm_free(vm);
}

// ------------------------------------------------------------------ bytecode format
static e7::Module compileModule(const char* src, bool debug) {
  e7::CompileOptions o;
  o.debugInfo = debug;
  e7::CompileOutput co = e7::compileProgram("main.ts", [&](const std::string& path, std::string& content) {
    if (path == "main.ts") { content = src; return true; }
    auto it = files.find(path);
    if (it == files.end()) return false;
    content = it->second;
    return true;
  }, o);
  CHECK(co.ok);
  return co.module;
}

// the line-table reader of VMs before debug information (copied from e7 1.0.1): returns the line
static uint32_t oldLineFor(const std::vector<uint32_t>& L, size_t pc, std::string& file, const e7::Module& m) {
  uint32_t line = 0;
  size_t i = 0;
  for (; i + 1 < L.size(); i += 2) {
    if (L[i] == e7::kLineExtMarker) break;
    if ((size_t)L[i] > pc) {
      while (i + 1 < L.size() && L[i] != e7::kLineExtMarker) i += 2;
      break;
    }
    line = L[i + 1];
  }
  if (i + 1 < L.size() && L[i] == e7::kLineExtMarker && L[i + 1] == e7::kLineExtFiles) {
    for (size_t j = i + 2; j + 3 < L.size(); j += 4) {
      if (L[j] != e7::kLineExtMarker || L[j + 2] != e7::kLineExtMarker) break;
      if ((size_t)L[j + 1] > pc) break;
      if (L[j + 3] < m.strs.size()) file = e7::utf16ToUtf8(m.strs[L[j + 3]]);
    }
  }
  return line;
}

static uint32_t rd32(const std::vector<uint8_t>& b, size_t at) {
  return (uint32_t)b[at] | ((uint32_t)b[at + 1] << 8) | ((uint32_t)b[at + 2] << 16) | ((uint32_t)b[at + 3] << 24);
}

static void testFormat() {
  e7::Module rel = compileModule(kMain, false);
  e7::Module dbg = compileModule(kMain, true);
  // debug information does not change the code
  CHECK(rel.funcs.size() == dbg.funcs.size());
  for (size_t i = 0; i < rel.funcs.size() && i < dbg.funcs.size(); i++) {
    CHECK(rel.funcs[i].code == dbg.funcs[i].code);
    CHECK(rel.funcs[i].lines == dbg.funcs[i].lines);
    CHECK(!rel.funcs[i].hasDebug && dbg.funcs[i].hasDebug);
  }
  std::vector<uint8_t> rb = e7::serializeModule(rel), db = e7::serializeModule(dbg);
  CHECK(rd32(rb, 8) == 0 && rd32(db, 8) == e7::kModuleFlagDebug);  // header flags
  // round trip
  e7::Module back;
  std::string err;
  CHECK(e7::deserializeModule(db.data(), db.size(), back, err));
  CHECK(e7::verifyModule(back, err));
  CHECK(e7::serializeModule(back) == db);
  for (size_t i = 0; i < back.funcs.size(); i++) {
    CHECK(back.funcs[i].lines == rel.funcs[i].lines);  // the extension is stripped from the table
    CHECK(back.funcs[i].debug.stmts.size() == dbg.funcs[i].debug.stmts.size());
    CHECK(back.funcs[i].debug.locals.size() == dbg.funcs[i].debug.locals.size());
  }
  // a reader from before debug information (it stops at the first group that is not a file segment)
  // maps every pc to the same line and file whether or not the debug extension follows the table
  for (auto& f : dbg.funcs) {
    std::vector<uint32_t> ext = f.lines;
    ext.push_back(e7::kLineExtMarker);
    ext.push_back(e7::kLineExtDebug);
    ext.push_back(3);
    ext.push_back(0x12345678);
    ext.push_back(e7::kLineExtMarker);
    ext.push_back(e7::kLineExtMarker);
    for (size_t pc = 0; pc < f.code.size(); pc++) {
      std::string fa = "main.ts", fb = "main.ts";
      CHECK(oldLineFor(f.lines, pc, fa, dbg) == oldLineFor(ext, pc, fb, dbg));
      CHECK(fa == fb);
    }
  }
  e7_vm* vm = newVm();
  e7_module* mod = nullptr;
  CHECK(e7_load(vm, db.data(), db.size(), &mod) == E7_OK);
  e7_vm_free(vm);
}

static e7::Module debugModule() { return compileModule(kMain, true); }

static size_t funcWithLocals(const e7::Module& m) {
  for (size_t i = 0; i < m.funcs.size(); i++)
    if (m.funcs[i].name == "add") return i;
  return 0;
}

static void expectReject(const char* what, const std::function<void(e7::Module&)>& mutate) {
  e7::Module m = debugModule();
  mutate(m);
  std::string err;
  bool ok = e7::verifyModule(m, err);
  if (ok) fprintf(stderr, "verifier accepted malformed debug info: %s\n", what);
  CHECK(!ok);
  // also through serialization + e7_load
  std::vector<uint8_t> b = e7::serializeModule(m);
  e7_vm* vm = newVm();
  e7_module* mod = nullptr;
  e7_status st = e7_load(vm, b.data(), b.size(), &mod);
  CHECK(st == E7_ERR_VERIFY || st == E7_ERR_LOAD);
  e7_vm_free(vm);
}

static void testVerifier() {
  expectReject("register out of range", [](e7::Module& m) { m.funcs[funcWithLocals(m)].debug.locals[0].reg = 60000; });
  expectReject("register kind", [](e7::Module& m) {
    auto& l = m.funcs[funcWithLocals(m)].debug.locals[0];
    l.vt = e7::VT_REF;
  });
  expectReject("cell flag on a number register", [](e7::Module& m) {
    m.funcs[funcWithLocals(m)].debug.locals[0].flags |= e7::kDebugLocalCell;
  });
  expectReject("bad flags", [](e7::Module& m) { m.funcs[funcWithLocals(m)].debug.locals[0].flags = 0x40; });
  expectReject("bad value type", [](e7::Module& m) { m.funcs[funcWithLocals(m)].debug.locals[0].vt = 200; });
  expectReject("range past the end", [](e7::Module& m) {
    auto& f = m.funcs[funcWithLocals(m)];
    f.debug.locals[0].endPc = (uint32_t)f.code.size() + 1;
  });
  expectReject("inverted range", [](e7::Module& m) {
    auto& l = m.funcs[funcWithLocals(m)].debug.locals[0];
    l.startPc = 5;
    l.endPc = 2;
  });
  expectReject("statement pc out of range", [](e7::Module& m) {
    auto& f = m.funcs[funcWithLocals(m)];
    f.debug.stmts.back().pc = (uint32_t)f.code.size();
  });
  expectReject("statements not increasing", [](e7::Module& m) {
    auto& f = m.funcs[funcWithLocals(m)];
    f.debug.stmts.push_back(f.debug.stmts[0]);
  });
  expectReject("statement inside an instruction", [](e7::Module& m) {
    // a CALL's argument word is not an instruction start
    for (auto& f : m.funcs)
      for (size_t pc = 0; pc < f.code.size(); pc++)
        if (e7::insOp(f.code[pc]) == e7::OP_CALL && e7::insC(f.code[pc]) > 0) {
          f.debug.stmts = {e7::DebugPos{(uint32_t)pc + 1, 1, 1}};
          return;
        }
  });
  expectReject("position pc out of range", [](e7::Module& m) {
    auto& f = m.funcs[funcWithLocals(m)];
    f.debug.pos.push_back(e7::DebugPos{(uint32_t)f.code.size(), 1, 1});
  });
  expectReject("upvalue names", [](e7::Module& m) { m.funcs[funcWithLocals(m)].debug.upvalNames = {"x"}; });

  // malformed encodings are rejected by the loader
  e7::Module m = debugModule();
  std::vector<uint8_t> good = e7::serializeModule(m);
  auto reload = [](std::vector<uint8_t> b) {
    // fix the CRC so that only the debug extension is wrong
    extern uint32_t testCrc(const uint8_t*, size_t);
    size_t n = b.size();
    uint32_t c = testCrc(b.data(), n - 8);
    for (int k = 0; k < 4; k++) b[n - 4 + k] = (uint8_t)(c >> (8 * k));
    e7::Module out;
    std::string err;
    return e7::deserializeModule(b.data(), b.size(), out, err);
  };
  CHECK(reload(good));
  // find the first debug extension header and corrupt its word count / payload
  size_t at = 0;
  for (size_t i = 0; i + 8 <= good.size(); i++)
    if (rd32(good, i) == e7::kLineExtMarker && rd32(good, i + 4) == e7::kLineExtDebug) { at = i; break; }
  CHECK(at > 0);
  if (at) {
    std::vector<uint8_t> b = good;
    b[at + 8] ^= 0x01;  // word count off by one
    CHECK(!reload(b));
    b = good;
    b[at + 11] = 0x7F;  // huge word count
    CHECK(!reload(b));
    b = good;
    uint32_t npos = rd32(good, at + 12);
    uint32_t big = npos + 0x1000;
    for (int k = 0; k < 4; k++) b[at + 12 + k] = (uint8_t)(big >> (8 * k));  // position count too big
    CHECK(!reload(b));
  }
}

uint32_t testCrc(const uint8_t* p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
  }
  return c ^ 0xFFFFFFFFu;
}

int main() {
  testBreakpointsAndStepping();
  testVariables();
  testClosuresAndImports();
  testBreakpointResolution();
  testStopOnEntryAndExceptions();
  testCaughtErrors();
  testSortCallbackSync();
  testPauseFromThread();
  testHostMode();
  testHostModeBudget();
  testWithoutDebugInfo();
  testNoDebugger();
  testFormat();
  testVerifier();
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("debug tests passed\n");
  return 0;
}
