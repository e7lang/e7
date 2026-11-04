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

#define E7_VERSION_MAJOR 1
#define E7_VERSION_MINOR 0
#define E7_VERSION_PATCH 0

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
  E7_ERR_STATE = 12      /* VM busy (suspended) or invalid state */
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

typedef struct e7_config {
  size_t memory_limit;          /* heap limit in bytes (default 256 MiB) */
  uint32_t stack_slots;         /* register stack size in 8-byte slots (default 262144) */
  uint32_t max_frames;          /* maximum call depth (default 20000) */
  int suspend_on_budget;        /* 1: budget exhaustion suspends instead of failing */
  int require_signature;        /* 1: e7_load() only accepts modules signed by a trusted key */
  const uint8_t* trusted_keys;  /* num_trusted_keys * 32 bytes of Ed25519 public keys */
  size_t num_trusted_keys;
} e7_config;

/* ---- lifecycle ---- */
E7_API void e7_config_init(e7_config* cfg);
E7_API e7_vm* e7_vm_new(const e7_config* cfg); /* cfg may be NULL; returns NULL on allocation failure */
E7_API void e7_vm_free(e7_vm* vm);
E7_API const char* e7_version(void);

/* ---- modules ---- */
/* Verifies (signature, structure, types), links natives and runs the module initializer. */
E7_API e7_status e7_load(e7_vm* vm, const uint8_t* data, size_t len, e7_module** out);
/* Compiles E7 source to bytecode (only when built with E7_WITH_COMPILER). On success *out is
 * allocated with malloc (free with e7_free); on failure *errors (if non-NULL) receives a
 * malloc'ed diagnostic text. */
E7_API e7_status e7_compile(const char* source, size_t len, const char* file_name, uint8_t** out, size_t* out_len,
                            char** errors);
/* Compile and load in one step (convenience for development). */
E7_API e7_status e7_load_source(e7_vm* vm, const char* source, size_t len, const char* file_name, e7_module** out);
E7_API void e7_free(void* p);

/* ---- calls ---- */
E7_API e7_status e7_call(e7_vm* vm, e7_module* mod, const char* function, const e7_value* args, size_t nargs,
                         e7_value* ret);
E7_API e7_status e7_resume(e7_vm* vm, e7_value* ret);
E7_API void e7_abort_suspended(e7_vm* vm);
/* Instruction budget for subsequent execution (<= 0: unlimited). */
E7_API void e7_set_budget(e7_vm* vm, int64_t ticks);
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

/* ---- host services ---- */
E7_API void e7_set_print(e7_vm* vm, void (*fn)(void* user, const char* text, size_t len), void* user);
/* KeyHandle resolution for e7:crypto (keys never enter script memory). Return 0 on success. */
E7_API void e7_set_key_provider(e7_vm* vm, int (*fn)(void* user, int64_t id, const uint8_t** key, size_t* len),
                                void* user);
E7_API void e7_seed_random(e7_vm* vm, uint64_t seed);
E7_API size_t e7_memory_used(e7_vm* vm);
E7_API void e7_collect_garbage(e7_vm* vm);

/* ---- module signing (Ed25519) ---- */
E7_API e7_status e7_sign_module(const uint8_t* e7b, size_t len, const uint8_t secret_seed[32], uint8_t** out,
                                size_t* out_len);
E7_API void e7_public_key(const uint8_t secret_seed[32], uint8_t public_key[32]);

#ifdef __cplusplus
}
#endif
#endif /* E7_E7_H */
