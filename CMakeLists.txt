cmake_minimum_required(VERSION 3.16)
project(e7vm VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(E7_WITH_COMPILER "Include the E7 compiler in the library (on-device compilation)" ON)
option(E7_BUILD_TOOLS "Build the e7 / e7c command line tools" ON)
option(E7_BUILD_TESTS "Build tests" ON)
option(E7_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(E7_FUZZ "Build libFuzzer targets (clang only)" OFF)
option(E7_NO_COMPUTED_GOTO "Use switch dispatch instead of computed goto" OFF)
option(E7_BUILD_SHARED "Build e7vm as a shared library" OFF)
option(E7_BUILD_JNI "Build the JNI binding (libe7vm_jni) for Android / JVM" OFF)
option(E7_BUILD_BENCH "Build the C API benchmarks in bench/ (e7_gc_pause)" OFF)
# testing only (CONTRIBUTING.md, "Stability checks"): never enable in a shipped build
option(E7_GC_STRESS "Collect garbage at every safepoint and every allocation where a collection is allowed" OFF)
option(E7_TSAN "Build with ThreadSanitizer (tests/threads_test)" OFF)
option(E7_COVERAGE "Build with clang source-based coverage (-fprofile-instr-generate -fcoverage-mapping)" OFF)

if(EMSCRIPTEN)
  # WebAssembly build (browser playground): no indirect goto in wasm, no CLI tools / tests.
  set(E7_NO_COMPUTED_GOTO ON CACHE BOOL "" FORCE)
  set(E7_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(E7_BUILD_TESTS OFF CACHE BOOL "" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
  add_compile_options(/W3 /EHsc /utf-8 /D_CRT_SECURE_NO_WARNINGS)
elseif(EMSCRIPTEN)
  # native WebAssembly exception handling (all current browsers and Node 17+)
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-trigraphs -fwasm-exceptions)
  add_link_options(-fwasm-exceptions)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-trigraphs -fexceptions)
endif()
if(NOT MSVC)
  # JS numbers (and fdlibm, for Math.* and **) round every operation: no contraction of a*b+c into
  # fused multiply-adds, which clang does by default (arm64) and GCC in its GNU modes. The files whose
  # results it would change also have a pragma, for builds without this flag (SwiftPM).
  add_compile_options(-ffp-contract=off)
endif()

# 32-bit x86: do double arithmetic in SSE2 registers, not with the x87 FPU, whose extended precision
# rounds twice (JS numbers and fdlibm need every operation rounded once to double).
if(NOT MSVC AND NOT EMSCRIPTEN AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  include(CheckCXXSourceCompiles)
  check_cxx_source_compiles("#if !defined(__i386__)\n#error not x86\n#endif\nint main() { return 0; }" E7_TARGET_I386)
  if(E7_TARGET_I386)
    add_compile_options(-msse2 -mfpmath=sse)
  endif()
endif()

if(E7_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()
if(E7_TSAN)
  if(E7_SANITIZE)
    message(FATAL_ERROR "E7_TSAN and E7_SANITIZE cannot be combined")
  endif()
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()
if(E7_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "E7_COVERAGE needs clang (llvm-cov)")
  endif()
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate)
endif()
if(E7_GC_STRESS)
  # every target sees it: tests reduce their iteration counts under stress (tests/workload.h)
  add_compile_definitions(E7_GC_STRESS=1)
endif()

# ---------------------------------------------------------------- third party crypto
file(GLOB_RECURSE BEARSSL_SRC CONFIGURE_DEPENDS third_party/bearssl/src/*.c)
add_library(e7_bearssl STATIC ${BEARSSL_SRC})
target_include_directories(e7_bearssl PUBLIC third_party/bearssl/inc PRIVATE third_party/bearssl/src)
# portable, UB-free byte access on every architecture; symbols stay internal to e7vm
set_target_properties(e7_bearssl PROPERTIES C_VISIBILITY_PRESET hidden)
target_compile_definitions(e7_bearssl PRIVATE BR_LE_UNALIGNED=0 BR_BE_UNALIGNED=0)
if(NOT MSVC)
  target_compile_options(e7_bearssl PRIVATE -w)
endif()
# BearSSL's hash / cipher vtables hold its functions cast to generic pointer types (C "objects");
# calls through them trip clang's -fsanitize=function (part of UBSan in OSS-Fuzz / ClusterFuzzLite
# and in -fsanitize=undefined with clang >= 17). Not instrumenting the callees skips that check.
include(CheckCCompilerFlag)
check_c_compiler_flag(-fno-sanitize=function E7_HAVE_NO_SANITIZE_FUNCTION)
if(E7_HAVE_NO_SANITIZE_FUNCTION)
  target_compile_options(e7_bearssl PRIVATE -fno-sanitize=function)
endif()

add_library(e7_monocypher STATIC third_party/monocypher/monocypher.c)
target_include_directories(e7_monocypher PUBLIC third_party/monocypher)
set_target_properties(e7_monocypher PROPERTIES C_VISIBILITY_PRESET hidden)
if(NOT MSVC)
  target_compile_options(e7_monocypher PRIVATE -w)
endif()

# ---------------------------------------------------------------- core / compiler / VM
set(E7_CORE_SRC src/core/utf.cpp src/core/numfmt.cpp src/core/serialize.cpp src/core/verify.cpp src/core/disasm.cpp
  third_party/fdlibm/ieee754.cpp)
set(E7_VM_SRC
  src/vm/vm.cpp src/vm/interp.cpp src/vm/debug.cpp src/vm/promise.cpp
  src/stdlib/core.cpp src/stdlib/string.cpp src/stdlib/array.cpp src/stdlib/map.cpp src/stdlib/math.cpp
  src/stdlib/bytes.cpp src/stdlib/bigint.cpp src/stdlib/json.cpp src/stdlib/encoding.cpp src/stdlib/date.cpp
  src/stdlib/test.cpp src/stdlib/typed.cpp src/stdlib/regexp.cpp
  src/regexp/regexp.cpp src/regexp/parser.cpp src/regexp/compiler.cpp src/regexp/unicode.cpp
  src/crypto/crypto.cpp src/crypto/keccak.cpp src/crypto/sign.cpp
  src/capi/capi.cpp)
set(E7_COMPILER_SRC
  src/compiler/parser.cpp src/compiler/types.cpp src/compiler/checker.cpp src/compiler/checker_stmt.cpp
  src/compiler/checker_expr.cpp src/compiler/checker_builtins.cpp src/compiler/checker_ns.cpp
  src/compiler/checker_generic.cpp
  src/compiler/codegen.cpp src/compiler/compiler.cpp)

set(E7_ALL_SRC ${E7_CORE_SRC} ${E7_VM_SRC})
if(E7_WITH_COMPILER)
  list(APPEND E7_ALL_SRC ${E7_COMPILER_SRC})
endif()

add_library(e7vm_objs OBJECT ${E7_ALL_SRC})
target_include_directories(e7vm_objs PUBLIC include PRIVATE src third_party/bearssl/inc third_party/monocypher)
if(E7_WITH_COMPILER)
  target_compile_definitions(e7vm_objs PUBLIC E7_WITH_COMPILER=1)
endif()
if(E7_NO_COMPUTED_GOTO)
  target_compile_definitions(e7vm_objs PRIVATE E7_NO_COMPUTED_GOTO=1)
elseif((CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18) OR
       (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17))
  # LLVM 15-17 CodeGenPrepare address-mode sinking miscompiles the computed-goto interpreter loop
  # (loads of trailing instruction words get the wrong address). Disable that transformation for
  # interp.cpp only; without this flag the source falls back to switch dispatch.
  set_source_files_properties(src/vm/interp.cpp PROPERTIES
    COMPILE_OPTIONS "-mllvm;-addr-sink-using-gep=false"
    COMPILE_DEFINITIONS "E7_LLVM_CGP_WORKAROUND=1")
endif()
if(E7_BUILD_SHARED)
  target_compile_definitions(e7vm_objs PUBLIC E7_SHARED PRIVATE E7_BUILDING)
  set_target_properties(e7vm_objs PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
endif()

# static library (always; used by tools and tests)
add_library(e7vm_static STATIC $<TARGET_OBJECTS:e7vm_objs>)
target_include_directories(e7vm_static PUBLIC include)
target_link_libraries(e7vm_static PUBLIC e7_bearssl e7_monocypher)
if(WIN32)
  target_link_libraries(e7vm_static PUBLIC advapi32)
endif()
if(E7_WITH_COMPILER)
  target_compile_definitions(e7vm_static PUBLIC E7_WITH_COMPILER=1)
endif()
set_target_properties(e7vm_static PROPERTIES OUTPUT_NAME e7vm_static)

if(E7_BUILD_SHARED)
  add_library(e7vm SHARED $<TARGET_OBJECTS:e7vm_objs>)
  target_include_directories(e7vm PUBLIC include)
  target_link_libraries(e7vm PRIVATE e7_bearssl e7_monocypher)
  target_compile_definitions(e7vm INTERFACE E7_SHARED)
  if(WIN32)
    target_link_libraries(e7vm PRIVATE advapi32)
  endif()
else()
  add_library(e7vm ALIAS e7vm_static)
endif()

# ---------------------------------------------------------------- tools
if(E7_BUILD_TOOLS)
  add_executable(e7 tools/e7.cpp tools/e7_test.cpp tools/e7_debug.cpp)
  target_link_libraries(e7 PRIVATE e7vm_static)
  target_include_directories(e7 PRIVATE src)
  add_executable(e7c tools/e7c.cpp)
  target_link_libraries(e7c PRIVATE e7vm_static)
  target_include_directories(e7c PRIVATE src)
  target_compile_definitions(e7 PRIVATE E7_VERSION="${PROJECT_VERSION}")
  target_compile_definitions(e7c PRIVATE E7_VERSION="${PROJECT_VERSION}")
endif()

if(EMSCRIPTEN)
  # e7wasm.js + e7wasm.wasm: ES module exporting createE7() (see tools/e7wasm.cpp)
  add_executable(e7wasm tools/e7wasm.cpp)
  target_link_libraries(e7wasm PRIVATE e7vm_static)
  target_include_directories(e7wasm PRIVATE src)
  target_link_options(e7wasm PRIVATE
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createE7 -sENVIRONMENT=web,worker,node
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=32MB -sSTACK_SIZE=4MB
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,HEAPU8
    # playground API, then the structured API used by the npm package (packages/e7), then the
    # debugger API (docs/DEBUGGING.md)
    -sEXPORTED_FUNCTIONS=_e7w_run,_e7w_output,_e7w_result,_e7w_error,_e7w_bytecode_size,_e7w_version,_e7w_disasm,_malloc,_free,_e7w_files_clear,_e7w_file_add,_e7w_compile,_e7w_diagnostics,_e7w_bytes_ptr,_e7w_bytes_len,_e7w_disasm_bytes,_e7w_exports,_e7w_args_clear,_e7w_arg_number,_e7w_arg_bool,_e7w_arg_null,_e7w_arg_string,_e7w_arg_bytes,_e7w_arg_int,_e7w_exec,_e7w_output_json,_e7w_result_json,_e7w_compile_ex,_e7w_inspect,_e7w_dbg_create,_e7w_dbg_set_breakpoints,_e7w_dbg_breakpoints,_e7w_dbg_break_on_error,_e7w_dbg_break_on_caught,_e7w_dbg_launch,_e7w_dbg_resume,_e7w_dbg_pause,_e7w_dbg_stack,_e7w_dbg_scopes,_e7w_dbg_variables,_e7w_dbg_evaluate,_e7w_dbg_terminate)
  set_target_properties(e7wasm PROPERTIES SUFFIX ".js")
endif()

if(E7_BUILD_JNI)
  add_library(e7vm_jni SHARED platform/android/jni/e7_jni.cpp)
  target_link_libraries(e7vm_jni PRIVATE e7vm_static)
  if(NOT ANDROID)
    find_package(JNI REQUIRED)
    target_include_directories(e7vm_jni PRIVATE ${JNI_INCLUDE_DIRS})
  else()
    target_link_libraries(e7vm_jni PRIVATE log)
  endif()
endif()

if(E7_BUILD_BENCH AND E7_WITH_COMPILER)
  # GC pause benchmark (README.md, performance); compiles its script with e7_load_source
  add_executable(e7_gc_pause bench/gc_pause.cpp)
  target_link_libraries(e7_gc_pause PRIVATE e7vm_static)
endif()

if(E7_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(E7_FUZZ)
  add_subdirectory(fuzz)
endif()

if(E7_BUILD_SHARED)
  install(TARGETS e7vm ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
endif()
install(TARGETS e7vm_static e7_bearssl e7_monocypher ARCHIVE DESTINATION lib)
install(FILES include/e7/e7.h DESTINATION include/e7)
