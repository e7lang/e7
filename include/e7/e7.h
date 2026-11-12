/*
 * e7vm — embeddable virtual machine for the E7 language (a strict, statically typed TypeScript subset).
 *
 * C API. All functions are safe to call from C, C++, Objective-C/Swift (via a module map),
 * Kotlin/Java (via JNI) and C# (via P/Invoke).
 *
 * Threading: an e7_vm is single-threaded. Use one VM per thread; VMs share no state.
 * Memory: strings / byte arrays returned in e7_value are owned by the VM and stay valid
 *         until the next call into the same VM.
 */
#ifndef E7_E7_H
#define E7_E7_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(E7_SHARED)
#  ifdef E7_BUILDING
#    define E7_API __declspec(dllexport)
#  else
#    define E7_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__)
#  define E7_API __attribute__((visibility("default")))
#else
#  define E7_API
#endif

#define E7_VERSION_MAJOR 0
#define E7_VERSION_MINOR 1
#define E7_VERSION_PATCH 5

typedef struct e7_vm e7_vm;
typedef struct e7_module e7_module;

typedef enum e7_status {
  E7_OK = 0,
  E7_SUSPENDED = 1,      /* instruction budget exhausted; call e7_resume() */
  E7_ERR_RUNTIME = 2,    /* runtime error / abort / throw in the script */
  E7_ERR_BUDGET = 3,     /* instruction budget exhausted (suspension disabled) */
  E7_ERR_MEMORY = 4,     /* memory limit exceeded */
  E7_ERR_STACK = 5,      /* call depth / stack limit exceeded */
  E7_ERR_LOAD = 6,       /* malformed bytecode or unknown native */
  E7_ERR_VERIFY = 7,     /* bytecode verification failed */
  E7_ERR_SIGNATURE = 8,  /* missing or invalid module signature */
  E7_ERR_NOT_FOUND = 9,  /* exported function not found */
  E7_ERR_ARGS = 10,      /* argument count / type mismatch */
  E7_ERR_COMPILE = 11,   /* compilation error */
  E7_ERR_STATE = 12,     /* VM busy (suspended) or invalid state */
  E7_PAUSED = 13,        /* stopped by an attached debugger (pause-to-host mode); see e7_debug_resume() */
  E7_ERR_VERSION = 14,   /* module format or required e7vm version not supported by this VM */
  E7_PENDING = 15        /* the call waits for host operations (async natives): settle their tickets with
                            e7_settle / e7_settle_error, then call e7_resume() */
} e7_status;

typedef enum e7_type {
  E7_VOID = 0,
  E7_NUMBER = 1,   /* double */
  E7_BOOL = 2,
  E7_INT = 3,      /* signed 64-bit (i8..i64) */
  E7_UINT = 4,     /* unsigned 64-bit (u8..u64) */
  E7_STRING = 5,   /* UTF-8 */
  E7_BYTES = 6,    /* Uint8Array */
  E7_HANDLE = 7,   /* KeyHandle (opaque id resolved by the key provider) */
  E7_NULL = 8
} e7_type;

typedef struct e7_value {
  e7_type type;
  union {
    double num;
    int boolean;
    int64_t i;
    uint64_t u;
    struct { const char* ptr; size_t len; } str;
    struct { const uint8_t* ptr; size_t len; } bytes;
    int64_t handle;
  } v;
} e7_value;

/* Structs passed into the library start with struct_size = sizeof(the struct), set by the matching
 * *_init function: always call it before filling in fields, so that a newer library can tell
 * which fields an older caller knows about. */
typedef struct e7_config {
  size_t struct_size;           /* sizeof(e7_config); set by e7_config_init() */
  size_t memory_limit;          /* heap limit in bytes (default 256 MiB) */
  uint32_t stack_slots;         /* register stack size in 8-byte slots (default 262144) */
  uint32_t max_frames;          /* maximum call depth (default 20000) */
  int suspend_on_budget;        /* 1: budget exhaustion suspends instead of failing */
  int require_signature;        /* 1: e7_load() only accepts modules signed by a trusted key */
  const uint8_t* trusted_keys;  /* num_trusted_keys * 32 bytes of Ed25519 public keys */
  size_t num_trusted_keys;
  uint32_t gc_growth_percent;   /* next collection when the heap reaches this % of the live size after the
                                   previous one (default 200, minimum 125; lower = less memory, more GCs) */
} e7_config;

/* ---- lifecycle ---- */
E7_API void e7_config_init(e7_config* cfg);
/* cfg may be NULL (defaults). Returns NULL on allocation failure or an invalid cfg->struct_size. */
E7_API e7_vm* e7_vm_new(const e7_config* cfg);
E7_API void e7_vm_free(e7_vm* vm);
E7_API const char* e7_version(void);            /* e.g. "0.1.5" */
E7_API uint32_t e7_version_number(void);        /* major << 16 | minor << 8 | patch */

/* ---- modules ---- */
/* Verifies (signature, structure, types), links natives and runs the module initializer.
 * E7_ERR_VERSION: the module has another bytecode format or needs a newer e7vm (its `requires`
 * header field; see e7_inspect). */
E7_API e7_status e7_load(e7_vm* vm, const uint8_t* data, size_t len, e7_module** out);

/* Reads a module's header without loading it (for example to pick a module the app's VM can run).
 * Checks the magic and the checksum; fills *info even when it returns E7_ERR_VERSION. */
typedef struct e7_module_info {
  size_t struct_size;          /* sizeof(e7_module_info); set by the caller */
  uint32_t format;             /* bytecode format identifier (this VM reads 3) */
  uint32_t requires_version;   /* minimum e7vm version, major << 16 | minor << 8 | patch */
  int is_signed;               /* 1: an Ed25519 signature trailer is present (not verified here) */
  uint8_t key_id[8];           /* first 8 bytes of SHA-256(public key) of the signing key */
  int has_debug_info;          /* 1: compiled with debug information */
} e7_module_info;
/* E7_OK: this VM can load the format and version; E7_ERR_VERSION: it cannot; E7_ERR_LOAD: not an
 * e7b module or corrupted. */
E7_API e7_status e7_inspect(const uint8_t* data, size_t len, e7_module_info* info);
/* Compiles E7 source to bytecode (only when built with E7_WITH_COMPILER). On success *out is
 * allocated with malloc (free with e7_free); on failure *errors (if non-NULL) receives a
 * malloc'ed diagnostic text. */
E7_API e7_status e7_compile(const char* source, size_t len, const char* file_name, uint8_t** out, size_t* out_len,
                            char** errors);
/* Extended compilation. flags: E7_COMPILE_DEBUG emits debug information (column numbers in stack
 * traces, statement boundaries and variable names for the debugger; see docs/DEBUGGING.md).
 * read_file (optional) resolves imports of other source files: return 0 and set *data / *len to a
 * malloc'ed buffer (e7 frees it), or nonzero if the file does not exist. opts may be NULL. */
#define E7_COMPILE_DEBUG 1u
typedef struct e7_compile_options {
  size_t struct_size;  /* sizeof(e7_compile_options); set by e7_compile_options_init() */
  uint32_t flags;
  int (*read_file)(void* user, const char* path, char** data, size_t* len);
  void* read_user;
} e7_compile_options;
E7_API void e7_compile_options_init(e7_compile_options* opts);
E7_API e7_status e7_compile_ex(const char* source, size_t len, const char* file_name, const e7_compile_options* opts,
                               uint8_t** out, size_t* out_len, char** errors);
/* Compile and load in one step (convenience for development). */
E7_API e7_status e7_load_source(e7_vm* vm, const char* source, size_t len, const char* file_name, e7_module** out);
E7_API void e7_free(void* p);

/* ---- calls ---- */
E7_API e7_status e7_call(e7_vm* vm, e7_module* mod, const char* function, const e7_value* args, size_t nargs,
                         e7_value* ret);
/* Continues a call that returned E7_SUSPENDED (budget) or E7_PENDING (after settling tickets). */
E7_API e7_status e7_resume(e7_vm* vm, e7_value* ret);
/* Abandons a suspended or pending call (its open tickets can no longer be settled). */
E7_API void e7_abort_suspended(e7_vm* vm);
/* Instruction budget for subsequent execution (<= 0: unlimited). */
E7_API void e7_set_budget(e7_vm* vm, int64_t ticks);
/* Ticks charged since the VM was created, with any budget (also unlimited): one per call, the
 * length of a loop body per iteration, plus the cost of natives doing size-dependent work. Counts
 * every e7_load / e7_call / e7_resume; deterministic for a given module and input, so it is a
 * machine-independent measure of the work a script did. When the budget runs out, the ticks of
 * the step that exhausted it are counted up to the budget. */
E7_API uint64_t e7_ticks_used(e7_vm* vm);
E7_API const char* e7_last_error(e7_vm* vm);
E7_API int e7_has_function(e7_module* mod, const char* function);

/* ---- host natives ----
 * signature: parameter type codes, '>', return code.
 *   n number  b boolean  s string  y Uint8Array  h KeyHandle  v void (return only)
 *   1 i8  2 u8  3 i16  4 u16  5 i32  6 u32  7 i64  8 u64
 * Scripts declare them with:  declare function name(a: string, b: number): string;
 * Must be registered before e7_load(). */
typedef e7_status (*e7_native_fn)(e7_vm* vm, const e7_value* args, size_t nargs, e7_value* ret, void* user);
E7_API e7_status e7_register_native(e7_vm* vm, const char* name, const char* signature, e7_native_fn fn, void* user);
/* From inside a native: set the error message reported when the native returns E7_ERR_RUNTIME. */
E7_API void e7_native_error(e7_vm* vm, const char* message);

/* ---- async host functions ----
 * `declare function fetchKey(id: string): Promise<Uint8Array>` is an async native: the signature is
 * written as for e7_register_native with the promise's value type as the return type ("s>y"). When the
 * script calls it, the VM creates a pending promise and calls `fn` with the arguments and a ticket. The
 * host settles the ticket, at once or later, on the VM's thread, with e7_settle / e7_settle_error. A
 * call whose script still waits for open tickets returns E7_PENDING; settle them and call e7_resume().
 * Returning an error status from `fn` throws in the script (no ticket is open then). */
typedef e7_status (*e7_async_native_fn)(e7_vm* vm, const e7_value* args, size_t nargs, int64_t ticket, void* user);
E7_API e7_status e7_register_async_native(e7_vm* vm, const char* name, const char* signature, e7_async_native_fn fn,
                                          void* user);
/* Fulfills the ticket's promise with `value` (of the native's return type; NULL for void).
 * E7_ERR_STATE: unknown or already settled ticket; E7_ERR_ARGS: wrong value type. */
E7_API e7_status e7_settle(e7_vm* vm, int64_t ticket, const e7_value* value);
/* Rejects the ticket's promise with an Error (message). */
E7_API e7_status e7_settle_error(e7_vm* vm, int64_t ticket, const char* message);
/* Number of open tickets. */
E7_API size_t e7_open_tickets(e7_vm* vm);

/* ---- host services ---- */
E7_API void e7_set_print(e7_vm* vm, void (*fn)(void* user, const char* text, size_t len), void* user);
/* KeyHandle resolution for e7:crypto (keys never enter script memory). Return 0 on success. */
E7_API void e7_set_key_provider(e7_vm* vm, int (*fn)(void* user, int64_t id, const uint8_t** key, size_t* len),
                                void* user);
E7_API void e7_seed_random(e7_vm* vm, uint64_t seed);
E7_API size_t e7_memory_used(e7_vm* vm);
E7_API void e7_collect_garbage(e7_vm* vm);
/* Garbage collector statistics since the VM was created. */
typedef struct e7_gc_stats {
  size_t struct_size;         /* sizeof(e7_gc_stats); set by the caller */
  uint64_t collections;
  uint64_t total_pause_ns;
  uint64_t max_pause_ns;      /* longest single collection */
  size_t heap_bytes;          /* currently allocated (same as e7_memory_used) */
  size_t peak_heap_bytes;
} e7_gc_stats;
E7_API e7_status e7_gc_stats_get(e7_vm* vm, e7_gc_stats* out);

/* ---- debugging (see docs/DEBUGGING.md) ----
 * Attach a debugger to a VM before loading modules (so that breakpoints in top-level code work).
 * While a debugger is attached the VM runs a debug build of its interpreter; without one there is
 * no cost at all. Breakpoints, stepping and variable names work best with modules compiled with
 * debug information (E7_COMPILE_DEBUG, `e7c --debug`); without it, stops happen at line starts and
 * only globals and upvalues have names.
 *
 * Two execution models:
 *  - synchronous: e7_debug_attach(vm, fn, user) with a callback. At each stop the callback runs on
 *    the VM's thread with the script frozen; it may call the inspection functions below and returns
 *    how to continue. Works for every stop, including module initialization (e7_load) and callbacks
 *    from natives (Array.sort comparators).
 *  - pause-to-host: e7_debug_attach(vm, NULL, NULL). A stop makes e7_load / e7_call / e7_resume
 *    return E7_PAUSED; inspect, then continue with e7_debug_resume(). When e7_load returns
 *    E7_PAUSED, *out is already set; the module is usable once the initializer has finished.
 *    Breakpoints inside callbacks from natives (sort comparators) are skipped in this model.
 * All functions except e7_debug_pause must be called on the VM's thread. Strings returned by the
 * inspection functions stay valid until the same function is called again or execution resumes.
 * Variable references (ref) stay valid while the script stays paused. */
typedef enum e7_debug_reason {
  E7_STOP_BREAKPOINT = 1,
  E7_STOP_STEP = 2,       /* a step finished */
  E7_STOP_DEBUGGER = 3,   /* `debugger;` statement */
  E7_STOP_PAUSE = 4,      /* e7_debug_pause() */
  E7_STOP_EXCEPTION = 5,  /* a runtime error is about to terminate the script (message in e7_debug_stop) */
  E7_STOP_ENTRY = 6       /* e7_debug_stop_on_entry() */
} e7_debug_reason;

typedef enum e7_debug_action {
  E7_DEBUG_CONTINUE = 0,
  E7_DEBUG_STEP_IN = 1,    /* stop at the next statement (entering calls) */
  E7_DEBUG_STEP_OVER = 2,  /* stop at the next statement of this function or a caller */
  E7_DEBUG_STEP_OUT = 3,   /* stop at the next statement of a caller */
  E7_DEBUG_ABORT = 4       /* terminate the script: the call fails with E7_ERR_RUNTIME */
} e7_debug_action;

typedef struct e7_debug_frame {
  const char* function; /* function name ("<init>" for top-level code) */
  const char* file;     /* source file as in stack traces (relative to the entry file's directory) */
  uint32_t line;        /* 1-based; 0 = unknown */
  uint32_t column;      /* 1-based; 0 = unknown (no debug information) */
} e7_debug_frame;

typedef struct e7_debug_stop {
  e7_debug_reason reason;
  const char* message;    /* E7_STOP_EXCEPTION: the error message; otherwise "" */
  e7_debug_frame where;   /* innermost frame */
  const int* breakpoints; /* E7_STOP_BREAKPOINT: ids of the breakpoints hit */
  size_t num_breakpoints;
} e7_debug_stop;

typedef struct e7_debug_scope {
  const char* name; /* "Locals", "Closure" (captured variables), "Globals" */
  uint32_t ref;     /* pass to e7_debug_variables() */
  size_t count;     /* number of variables */
} e7_debug_scope;

typedef struct e7_debug_variable {
  const char* name;
  const char* type;  /* number, boolean, i32, ..., string, null, undefined, Array, Map, Set, Object,
                        tuple, Uint8Array, bigint, function, Date, or the class name */
  const char* value; /* display text (like console.log) */
  uint32_t ref;      /* > 0: has children (elements / fields / entries), pass to e7_debug_variables() */
  size_t count;      /* number of children */
} e7_debug_variable;

typedef e7_debug_action (*e7_debug_fn)(e7_vm* vm, const e7_debug_stop* stop, void* user);

E7_API e7_status e7_debug_attach(e7_vm* vm, e7_debug_fn fn, void* user); /* fn NULL: pause-to-host */
E7_API e7_status e7_debug_detach(e7_vm* vm);                             /* not while running or paused */
E7_API void e7_debug_break_on_error(e7_vm* vm, int enable);              /* default 1 */
/* Also stop (E7_STOP_EXCEPTION) on runtime errors that a try/catch handler will take (default 0). The
 * synchronous handler stops where the error is raised; pause-to-host stops before the handler runs, with
 * the frames of the error still on the stack (not for errors caught inside a native callback, such as an
 * Array.sort comparator). Continuing (or stepping) then runs the catch block. */
E7_API void e7_debug_break_on_caught_errors(e7_vm* vm, int enable);
/* Breakpoint at the first statement on or after `line` of `file` (a path that ends with the file
 * name as shown in stack traces, e.g. "/src/lib/util.ts" for "lib/util.ts"). Returns its id (> 0),
 * or 0 if no debugger is attached. Breakpoints in modules loaded later are resolved at load. */
E7_API int e7_debug_set_breakpoint(e7_vm* vm, const char* file, uint32_t line);
/* 1 if the breakpoint exists; *verified: resolved to code; *actual_line: the line it stops at. */
E7_API int e7_debug_breakpoint_info(e7_vm* vm, int id, int* verified, uint32_t* actual_line);
E7_API e7_status e7_debug_clear_breakpoint(e7_vm* vm, int id);
E7_API void e7_debug_clear_breakpoints(e7_vm* vm);
/* Stop at the next statement. Thread-safe: may be called from any thread while the VM runs. */
E7_API void e7_debug_pause(e7_vm* vm);
/* Stop at the first statement executed from now on (reason E7_STOP_ENTRY). */
E7_API void e7_debug_stop_on_entry(e7_vm* vm);
E7_API int e7_debug_is_paused(e7_vm* vm);
/* While paused (inside the callback, or after E7_PAUSED): */
E7_API e7_status e7_debug_stop_info(e7_vm* vm, e7_debug_stop* out);
E7_API size_t e7_debug_stack(e7_vm* vm, const e7_debug_frame** frames);          /* frames[0] innermost */
E7_API size_t e7_debug_scopes(e7_vm* vm, size_t frame, const e7_debug_scope** scopes);
/* Children of a scope or variable reference; count 0 = all from `start`. */
E7_API size_t e7_debug_variables(e7_vm* vm, uint32_t ref, size_t start, size_t count, const e7_debug_variable** vars);
/* A variable of the frame (locals, then captured, then globals) followed by .field, [index],
 * ['map key'], .length or .size. On failure returns E7_ERR_NOT_FOUND (e7_last_error() says why). */
E7_API e7_status e7_debug_evaluate(e7_vm* vm, size_t frame, const char* expression, const e7_debug_variable** result);
/* Pause-to-host mode: continue after E7_PAUSED (like e7_resume, with a step action). */
E7_API e7_status e7_debug_resume(e7_vm* vm, e7_debug_action action, e7_value* ret);

/* ---- module signing (Ed25519) ---- */
E7_API e7_status e7_sign_module(const uint8_t* e7b, size_t len, const uint8_t secret_seed[32], uint8_t** out,
                                size_t* out_len);
E7_API void e7_public_key(const uint8_t secret_seed[32], uint8_t public_key[32]);

/* ---- detached signatures (update manifests, configuration files; see docs/DEPLOYMENT.md) ----
 * Ed25519 over the domain-separated message "e7-detached-v1\0" || data, so that a detached signature
 * can never be confused with a module signature (or the other way round).
 * e7_sign_detached: E7_OK, or E7_ERR_ARGS for NULL pointers.
 * e7_verify_detached: E7_OK if sig was made by one of the num_trusted_keys * 32-byte public keys,
 * E7_ERR_SIGNATURE if not (or if no key is given), E7_ERR_ARGS for NULL pointers. data may be NULL
 * when len is 0. */
E7_API e7_status e7_sign_detached(const uint8_t* data, size_t len, const uint8_t secret_seed[32], uint8_t sig_out[64]);
E7_API e7_status e7_verify_detached(const uint8_t* data, size_t len, const uint8_t sig[64], const uint8_t* trusted_keys,
                                    size_t num_trusted_keys);

#ifdef __cplusplus
}
#endif
#endif /* E7_E7_H */
