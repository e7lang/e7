# Debugging E7 programs

E7 programs can be debugged with breakpoints, stepping and variable inspection:

- from the command line with **`e7 debug`** (a small gdb-like debugger),
- from a host application through the **C debug API** in [`include/e7/e7.h`](../include/e7/e7.h),
- from JavaScript (a VS Code debug adapter, a web IDE) through the **WebAssembly entry points** in
  [`tools/e7wasm.cpp`](../tools/e7wasm.cpp), which speak a small JSON protocol.

All three use the same debugger in the VM ([`src/vm/debug.h`](../src/vm/debug.h)). It costs nothing when no
debugger is attached (see [§6](#6-design-notes)).

## 1. Quick start

```ts
// prices.ts
function total(prices: number[]): number {
  let sum = 0;
  for (const p of prices) {
    sum = sum + p;
  }
  return sum;
}
export function main(): number {
  const items = [3, 4.5, 10];
  debugger;                       // stops here when a debugger is attached; a no-op otherwise
  return total(items);
}
```

```
$ e7 debug prices.ts
Stopped at entry, main (prices.ts:10:3)
     10 |   const items = [3, 4.5, 10];
(e7db) b 5
Breakpoint 1 at prices.ts:5
(e7db) c
debugger statement, main (prices.ts:11:3)
     11 |   debugger;                       // stops here when a debugger is attached; a no-op otherwise
(e7db) c
Breakpoint 1, total (prices.ts:5:5)
      5 |     sum = sum + p;
(e7db) locals
prices = [ 3, 4.5, 10 ]
sum = 0
p = 3
(e7db) p prices.length
prices.length = 3
(e7db) delete
Deleted all breakpoints
(e7db) finish
main (prices.ts:12:3)
     12 |   return total(items);
(e7db) c
[main returned 17.5]
[program exited]
```

## 2. Debug information

The compiler can add **debug information** to a module:

| | Debug information |
|---|---|
| `e7 script.ts` (runs from source), `e7 test`, `e7 debug` | always |
| `e7c script.ts` | no (release build) |
| `e7c --debug script.ts` (`-g`) | yes |
| `e7_compile()` / `e7_load_source()` | no |
| `e7_compile_ex()` with `E7_COMPILE_DEBUG` | yes |
| npm / WebAssembly `e7w_compile_ex(..., 1)` | yes |

It contains, per function: the source line **and column** of every instruction, the **statement boundaries**
(where stepping and breakpoints stop), the names of **local variables and parameters** with their register and
the range of instructions where they are live (block scopes), and the names of **captured variables**
(closures). Global variables always have names. Debug information does not change the generated code (a test
checks that the instructions are identical); it only adds data, typically 30–60 % of the module size.

Without debug information, a module can still be debugged: breakpoints and steps work per source line (the
line table), globals and captured variables are visible (captured ones as `$up0`, `$up1`, ...), but locals have
no names and stack traces have no columns.

**Column numbers in stack traces.** With debug information, runtime error traces show `file:line:column`:

```
index 5 out of range (length 1)
    at fail (prices.ts:16:12)
    at main (prices.ts:12:10)
```

Without it (release bytecode), the format is unchanged: `at fail (prices.ts:16)`. Columns are 1-based UTF-8
byte offsets, like compiler diagnostics. A call is located at the callee's name and a property access at the
property name (like V8); an index expression at its `[`.

**Compatibility.** The debug information is stored as an extension of each function's line table that older VMs
skip (see [BYTECODE.md](BYTECODE.md#1-file-format)), so a module compiled with `--debug` loads in any e7vm 1.x.
The module header sets flag bit 0 when debug information is present. The verifier checks the debug section like
code (register indices and kinds, instruction ranges, statement boundaries at instruction starts): it is
untrusted input too.

## 3. The `debugger;` statement

`debugger;` compiles to a `DEBUGGER` instruction. Without a debugger it does nothing (as in Node.js without an
inspector); with one, execution stops there (`E7_STOP_DEBUGGER`). A module that contains `debugger;` needs a VM
that knows the instruction (this release or later); older VMs reject it at load time ("invalid opcode").

## 4. Command-line debugger: `e7 debug`

```
e7 debug [--break file:line]... [--call fn] [--budget n] [--mem bytes] [--seed n] script.ts|.e7|.e7b
```

The script is compiled with debug information (a `.e7b` is used as is; compile it with `e7c --debug`). Without
`--break`, it stops at the first statement. Then top-level code runs, then `main()` (or `--call fn`).

| Command | |
|---|---|
| `b [file:]line` | breakpoint at the first statement on or after `line` (pending until the file's code is loaded) |
| `delete [n]` | delete breakpoint `n`, or all |
| `c` | continue |
| `n` | next: step over calls |
| `s` | step into calls |
| `finish` | step out: stop in the caller, right after the call |
| `bt` | call stack (`*` marks the selected frame) |
| `up`, `down`, `frame n` | select a frame for `locals`, `p`, `list` |
| `locals` | variables of the selected frame (and its captured variables) |
| `globals` | global variables of the module |
| `p expr` | a variable followed by `.field`, `[index]`, `['map key']`, `.length`, `.size` |
| `list` | source around the current line |
| `q` | quit (the script is aborted) |

An empty line repeats the last command. At the end of input the program runs to completion. Program output
goes to stdout as it is printed; a runtime error stops with `Runtime error: ...` before the program fails.

## 5. Embedding: the C debug API

### 5.1 Attaching and execution models

```c
e7_vm* vm = e7_vm_new(NULL);
e7_debug_attach(vm, on_stop, &my_state);    // before e7_load: breakpoints in top-level code work too
e7_debug_set_breakpoint(vm, "/src/app/main.ts", 12);
uint8_t* bc; size_t len; char* errs;
e7_compile_options o = {E7_COMPILE_DEBUG, read_file, NULL};
e7_compile_ex(src, src_len, "main.ts", &o, &bc, &len, &errs);
e7_load(vm, bc, len, &mod);                 // stops (calls on_stop) as the script runs
```

There are two ways to handle a stop:

**Synchronous callback** (`e7_debug_attach(vm, fn, user)`). At every stop `fn` runs on the VM's thread while the
script is frozen. It may call the inspection functions and returns an action:

```c
static e7_debug_action on_stop(e7_vm* vm, const e7_debug_stop* stop, void* user) {
  printf("stopped (%d) at %s:%u:%u in %s\n", stop->reason, stop->where.file, stop->where.line,
         stop->where.column, stop->where.function);
  const e7_debug_scope* scopes;
  size_t n = e7_debug_scopes(vm, 0, &scopes);            // frame 0 = innermost
  /* ... e7_debug_variables(vm, scopes[0].ref, 0, 0, &vars) ... */
  return E7_DEBUG_STEP_OVER;                              // or wait for the user's command here
}
```

The host can block inside the callback until the user decides (a UI thread posts commands to the VM thread);
`e7 debug` reads commands from stdin there. This model works for every stop: module initialization, calls,
and callbacks from natives (`Array.sort` comparators).

**Pause-to-host** (`e7_debug_attach(vm, NULL, NULL)`). A stop makes the running `e7_load` / `e7_call` /
`e7_resume` return **`E7_PAUSED`**, keeping the script's frames. The host inspects at leisure and continues
with `e7_debug_resume(vm, action, &ret)`, which returns the call's final status (or `E7_PAUSED` again):

```c
e7_status st = e7_call(vm, mod, "main", NULL, 0, &ret);
while (st == E7_PAUSED) {
  e7_debug_stop stop;
  e7_debug_stop_info(vm, &stop);
  /* show stop.where, e7_debug_stack(), ... ; wait for the user */
  st = e7_debug_resume(vm, E7_DEBUG_STEP_IN, &ret);
}
```

When `e7_load` returns `E7_PAUSED`, the module handle is already set; the module is usable once its top-level
code has finished (when a later `e7_debug_resume` returns `E7_OK`). While paused, `e7_call` returns
`E7_ERR_STATE`; `e7_abort_suspended` abandons the paused call. Built on the VM's suspend/resume machinery, this
model needs no threads and suits event loops and WebAssembly. Its one limitation: a stop inside a callback from a
native (an `Array.sort` comparator, an `assertThrows` body) cannot return to the host, because the native's C++
frames are in the way, so breakpoints and steps there are skipped.

Why both: the synchronous callback is the most robust (no restriction on where execution stops) and is the
natural model for native hosts and the CLI; pause-to-host keeps the host in control without threads or blocking,
which is what JavaScript and a Web/Node worker need. `E7_SUSPENDED` (instruction budget) and `E7_PAUSED`
combine: a debugged script can still run in time slices.

### 5.2 Breakpoints

`e7_debug_set_breakpoint(vm, file, line)` returns an id. The breakpoint stops at the **first statement on or
after `line`** (the first one in each function, so a line with several statements stops once).
`e7_debug_breakpoint_info` tells whether it is **verified** (resolved to code) and the actual line. Breakpoints
are kept across module loads: one set before `e7_load` resolves when the module is loaded.

File names in stack frames are relative to the entry file's directory (`main.ts`, `lib/util.ts`,
`../shared/x.ts`). A breakpoint's `file` matches when it is equal or when one is a path suffix of the other at a
`/` boundary, so an absolute path from an editor (`/home/me/app/lib/util.ts`) matches `lib/util.ts`; the
longest match wins.

### 5.3 Stepping, pausing, errors

| Action | Stops at |
|---|---|
| `E7_DEBUG_CONTINUE` | the next breakpoint, `debugger;` statement, pause request, or runtime error |
| `E7_DEBUG_STEP_IN` | the next statement, entering calls (including closures and sort comparators) |
| `E7_DEBUG_STEP_OVER` | the next statement in this function or a caller |
| `E7_DEBUG_STEP_OUT` | the caller, right after the call returns (mid-statement, like V8) |
| `E7_DEBUG_ABORT` | nowhere: the call fails with `E7_ERR_RUNTIME` "execution aborted by the debugger" |

A step that returns from the function also stops in the caller right after the call. Breakpoints are reported
when hit during a step. A step that is still pending when a host call ends carries over to the next call (stepping
past the end of the top-level code stops at the first statement of `main`).

- `e7_debug_pause(vm)` may be called **from any thread** while the script runs: it stops at the next statement
  (`E7_STOP_PAUSE`). It is an atomic flag checked at statement boundaries.
- `e7_debug_stop_on_entry(vm)` stops at the next statement executed (`E7_STOP_ENTRY`).
- With `e7_debug_break_on_error(vm, 1)` (the default), a runtime error that is about to terminate the script
  stops first (`E7_STOP_EXCEPTION`, `stop->message` is the error message) with all frames intact; continuing (any
  action) lets the error terminate the call as usual. Errors caught by `assertThrows` do not stop.

### 5.4 Inspection

While paused (inside the callback, or after `E7_PAUSED`):

| Function | Returns |
|---|---|
| `e7_debug_stop_info` | reason, error message, innermost frame, ids of the breakpoints hit |
| `e7_debug_stack` | frames, innermost first: function, file, line, column |
| `e7_debug_scopes(frame)` | `Locals` (parameters, `this`, block-scoped variables live at that point; the innermost of shadowed names), `Closure` (captured variables), `Globals` |
| `e7_debug_variables(ref, start, count)` | children of a scope or of an expandable value, with paging |
| `e7_debug_evaluate(frame, expr)` | a variable followed by `.field`, `[i]`, `['key']`, `.length`, `.size` |

Every variable has a `name`, a `type` (`number`, `boolean`, `i32`, ..., `string`, `null`, `undefined`, `Array`,
`Map`, `Set`, `Object` (object literal), `tuple`, `Uint8Array`, `bigint`, `function`, `Date`, or the class name),
a display `value` (the `console.log` format, nested values abbreviated), and a `ref` (> 0 when it has children:
array elements, fields, Map entries (named by their key), Set elements, captured variables of a function) with
the number of children in `count`. References stay valid while the script stays paused (the same object keeps
the same reference); they are invalidated when it resumes. Returned strings stay valid until the same function
is called again or the script resumes. Inspection never runs script code and never allocates in the VM heap.

All debug functions except `e7_debug_pause` must be called on the VM's thread. `e7_debug_detach` restores the
normal interpreter (not while running or paused).

## 6. Design notes

- **Zero cost without a debugger.** The interpreter loop is a template instantiated twice. The normal instance
  is unchanged; the debug instance (used only while a debugger is attached) checks, before each instruction, one
  flag byte of that instruction (statement boundary / breakpoint / `debugger;`) against a mask of what currently
  matters (breakpoints always; statement boundaries while stepping or when a pause is requested). Only a match
  calls into the debugger. Benchmarks show no difference in the normal interpreter (the comparison is in the
  pull request description); the debug interpreter runs at roughly the same speed until it stops.
- **Stops happen in script code only**, before an instruction (statement boundary, breakpoint, `debugger;`) or
  when an error is raised. Execution never pauses inside a host function.
- **Robustness against hostile bytecode.** The debug section is verified at load time like code; the inspector
  checks every object's runtime type before reading it, so a malformed (but verified) mapping can at worst show
  a wrong value. Fuzzers (`fuzz/fuzz_load.cpp`, `tests/fuzz_load.cpp`) attach a debugger to mutated modules and
  inspect everything at every stop.

## 7. WebAssembly / JavaScript: the JSON protocol

The WebAssembly build exports a debugger session API (one session at a time) designed for a VS Code debug
adapter running the VM in a Web Worker or Node worker thread. It uses the pause-to-host model: every call that
runs script code returns an **event** (JSON). The existing exports are unchanged. All strings are UTF-8; a
returned `const char*` stays valid until the next `e7w_*` call.

| Export | |
|---|---|
| `int e7w_compile_ex(source, len, fileName, flags)` | like `e7w_compile`; `flags` 1 = debug information. Imports come from `e7w_file_add`. Bytecode: `e7w_bytes_ptr/len` |
| `void e7w_dbg_create(memLimit, seed, slice)` | new session (ends the previous one). `slice > 0`: run at most `slice` instructions per call, then return a `running` event |
| `const char* e7w_dbg_set_breakpoints(file, lines)` | replace all breakpoints of `file`; `lines` = `"3,17,42"`. Returns `[{"id","requestedLine","line","verified"}]` |
| `const char* e7w_dbg_breakpoints()` | `[{"id","file","line","verified"}]`: breakpoints set before launch become verified when the module loads |
| `void e7w_dbg_break_on_error(enable)` | stop on runtime errors (default on) |
| `const char* e7w_dbg_launch(data, len, call, stopOnEntry)` | load the `.e7b`, run top-level code, then call `call` (`""` = `main` if exported) with the `e7w_arg_*` arguments. Returns an event |
| `const char* e7w_dbg_resume(action)` | `"continue"`, `"next"`, `"stepIn"`, `"stepOut"`. Returns an event |
| `void e7w_dbg_pause()` | stop at the next statement (takes effect in the next slice) |
| `const char* e7w_dbg_stack()` | `[{"id","name","file","line","column"}]`, `id` 0 = innermost |
| `const char* e7w_dbg_scopes(frameId)` | `[{"name","ref","count"}]` |
| `const char* e7w_dbg_variables(ref, start, count)` | `[{"name","type","value","ref","count"}]` (`count` 0 = all) |
| `const char* e7w_dbg_evaluate(frameId, expr)` | `{"ok":true,"name","type","value","ref","count"}` or `{"ok":false,"error"}` |
| `const char* e7w_dbg_terminate()` | end the session; returns a `terminated` event |

Events:

```json
{"event":"stopped","reason":"breakpoint","message":"","breakpoints":[1],
 "frame":{"name":"add","file":"lib/math.ts","line":12,"column":3},"output":["printed line", "..."]}
{"event":"running","output":[]}
{"event":"terminated","status":0,"result":{"type":"number","value":28},"error":"","output":[]}
```

- `reason`: `entry`, `breakpoint`, `step`, `debugger`, `pause`, `exception` (`message` = the error).
- `output`: lines printed by the script since the previous event.
- `running` (only with `slice > 0`): the slice is used up; call `e7w_dbg_resume("continue")` to go on. Between
  slices the worker can handle messages, e.g. a pause request (`e7w_dbg_pause()` then resume). Time slicing
  applies to calls, not to top-level code.
- `terminated`: `status` is an `e7_status` (0 ok, 2 runtime error, 3 budget, ...), `result` as in
  `e7w_result_json`, `error` the message with the stack trace.

Mapping to the Debug Adapter Protocol: `setBreakpoints` → `e7w_dbg_set_breakpoints` (send `breakpoint` events
from `e7w_dbg_breakpoints()` after launch), `configurationDone` → `e7w_dbg_launch`, `continue` / `next` /
`stepIn` / `stepOut` → `e7w_dbg_resume`, `stackTrace` / `scopes` / `variables` / `evaluate` → the functions of the
same name (the `ref` values are DAP `variablesReference`s, `count` gives `indexedVariables` / `namedVariables`),
`pause` → `e7w_dbg_pause`, `disconnect` → `e7w_dbg_terminate`. A worker loop:

```js
const call = (name, ret, types, args) => m.ccall(name, ret, types, args);
call("e7w_dbg_create", null, ["number", "number", "number"], [64 << 20, -1, 1_000_000]);
call("e7w_dbg_set_breakpoints", "string", ["string", "string"], ["main.ts", "12,20"]);
let ev = JSON.parse(call("e7w_dbg_launch", "string", ["number", "number", "string", "number"], [ptr, len, "", 0]));
while (ev.event === "running") {            // let other messages (pause, setBreakpoints) in between
  await new Promise((r) => setTimeout(r));
  ev = JSON.parse(call("e7w_dbg_resume", "string", ["string"], ["continue"]));
}
if (ev.event === "stopped") postMessage(ev);  // then answer stackTrace / variables requests
```

The same file builds natively (`E7W_NATIVE_MAIN`, target `e7wasm_native` in the test build) with a command mode
used by `tests/debug/wasm_debug_test.py`: `e7wasm_native prog.ts --dbg "bp prog.ts 12" "launch" "stack" ...`.

## 8. Limitations (phase 1)

- No conditional breakpoints, logpoints, watch expressions with operators, or changing variables.
- Pause-to-host mode skips stops inside callbacks from natives (sort comparators, `assertThrows` bodies).
- Pausing happens at statements: a long-running native call (e.g. sorting a huge array) finishes first.
- `e7_debug_pause` from another thread is supported natively; in single-threaded WebAssembly use time slices.
