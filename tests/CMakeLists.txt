# E7_GC_STRESS builds (a collection at every safepoint and allocation, CONTRIBUTING.md) run fewer fuzz
# iterations and leave out tests/cases/lib/gc_stress.ts (see "stability" below)
set(E7_FUZZ_COMPILE_ITERS 1500)
set(E7_LANG_TEST_ARGS "")
if(E7_GC_STRESS)
  set(E7_FUZZ_COMPILE_ITERS 200)
  set(E7_LANG_TEST_ARGS --exclude gc_stress.ts)
endif()

add_executable(capi_test capi_test.cpp)
target_link_libraries(capi_test PRIVATE e7vm_static)
add_test(NAME capi_test COMMAND capi_test)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  find_program(NODE_EXECUTABLE node)
  if(NODE_EXECUTABLE AND TARGET e7)
    add_test(NAME language_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py --e7 $<TARGET_FILE:e7> --node ${NODE_EXECUTABLE} ${E7_LANG_TEST_ARGS})
  endif()
  if(TARGET e7 AND TARGET e7c)
    # the AI skill (skills/e7): every code block / example must behave as the skill claims
    add_test(NAME skill_check COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/skill/check_skill.py
             --e7 $<TARGET_FILE:e7> --e7c $<TARGET_FILE:e7c> --root ${CMAKE_SOURCE_DIR})
    # `e7 test` runner and CLI diagnostics (source excerpts)
    add_test(NAME cli_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cli/cli_test.py --e7 $<TARGET_FILE:e7> --e7c $<TARGET_FILE:e7c>)
    # `e7 debug` command-line debugger, driven through stdin
    add_test(NAME cli_debug_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/debug/cli_debug_test.py --e7 $<TARGET_FILE:e7> --e7c $<TARGET_FILE:e7c>)
  endif()
  if(E7_WITH_COMPILER)
    # the WebAssembly entry points (tools/e7wasm.cpp) built natively: debugger JSON protocol
    add_executable(e7wasm_native ${CMAKE_SOURCE_DIR}/tools/e7wasm.cpp)
    target_compile_definitions(e7wasm_native PRIVATE E7W_NATIVE_MAIN=1)
    target_link_libraries(e7wasm_native PRIVATE e7vm_static)
    target_include_directories(e7wasm_native PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME wasm_debug_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/debug/wasm_debug_test.py --e7wasm $<TARGET_FILE:e7wasm_native>)
  endif()
endif()

add_executable(fuzz_load fuzz_load.cpp)
target_link_libraries(fuzz_load PRIVATE e7vm_static)
target_include_directories(fuzz_load PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME fuzz_load COMMAND fuzz_load 3000)

if(E7_BUILD_SHARED)
  # the C API test also runs against the shared library (exports only the C API)
  add_executable(capi_test_shared capi_test.cpp)
  target_link_libraries(capi_test_shared PRIVATE e7vm)
  add_test(NAME capi_test_shared COMMAND capi_test_shared)
endif()

if(E7_WITH_COMPILER)
  add_executable(fuzz_compile fuzz_compile.cpp)
  target_link_libraries(fuzz_compile PRIVATE e7vm_static)
  file(GLOB E7_FUZZ_CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/cases/lang/*.ts ${CMAKE_CURRENT_SOURCE_DIR}/cases/lib/*.ts)
  add_test(NAME fuzz_compile COMMAND fuzz_compile ${E7_FUZZ_COMPILE_ITERS} ${E7_FUZZ_CORPUS})
endif()

add_executable(locale_test locale_test.cpp)
target_link_libraries(locale_test PRIVATE e7vm_static)
add_test(NAME locale_test COMMAND locale_test)

# hand-crafted malicious modules (security review regressions) must be rejected without crashing
add_executable(malicious_test malicious_test.cpp)
target_link_libraries(malicious_test PRIVATE e7vm_static)
file(GLOB E7_MALICIOUS ${CMAKE_CURRENT_SOURCE_DIR}/malicious/*.e7b)
add_test(NAME malicious_test COMMAND malicious_test ${E7_MALICIOUS})

# bytecode compatibility corpus (docs/BYTECODE.md): 0.1 modules are rejected with E7_ERR_VERSION,
# modules from every 0.2.x release keep loading and printing the same output
add_executable(compat_test compat_test.cpp)
target_link_libraries(compat_test PRIVATE e7vm_static)
file(GLOB E7_COMPAT_REJECT ${CMAKE_CURRENT_SOURCE_DIR}/compat/0.1.*/*.e7b)
file(GLOB E7_COMPAT_RUN ${CMAKE_CURRENT_SOURCE_DIR}/compat/0.2.*/*.e7b)
add_test(NAME compat_reject COMMAND compat_test reject ${E7_COMPAT_REJECT})
if(E7_COMPAT_RUN)
  add_test(NAME compat_run COMMAND compat_test run ${E7_COMPAT_RUN})
endif()

# debugger: C debug API, debug information in bytecode, verifier checks of debug sections
if(E7_WITH_COMPILER)
  find_package(Threads REQUIRED)
  add_executable(debug_test debug_test.cpp)
  target_link_libraries(debug_test PRIVATE e7vm_static Threads::Threads)
  target_include_directories(debug_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  add_test(NAME debug_test COMMAND debug_test)
endif()

# try/catch handler tables: the verifier rejects malformed ones
if(E7_WITH_COMPILER)
  add_executable(verify_handlers_test verify_handlers_test.cpp)
  target_link_libraries(verify_handlers_test PRIVATE e7vm_static)
  target_include_directories(verify_handlers_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  add_test(NAME verify_handlers_test COMMAND verify_handlers_test)
endif()

add_executable(capi_reentry_test capi_reentry_test.cpp)
target_link_libraries(capi_reentry_test PRIVATE e7vm_static)
if(E7_WITH_COMPILER)
  add_test(NAME capi_reentry_test COMMAND capi_reentry_test)
endif()

# replay the libFuzzer targets (fuzz/) over the test corpora with the regular toolchain, so the fuzz
# entry points are exercised on every platform and under the sanitizer jobs
set(E7_FUZZ_REPLAY load compile)
if(E7_WITH_COMPILER)
  list(APPEND E7_FUZZ_REPLAY natives)  # compiles its harness module with e7_compile
endif()
foreach(t ${E7_FUZZ_REPLAY})
  add_executable(e7_fuzz_${t}_replay ${CMAKE_SOURCE_DIR}/fuzz/fuzz_${t}.cpp ${CMAKE_SOURCE_DIR}/fuzz/replay_main.cpp)
  target_link_libraries(e7_fuzz_${t}_replay PRIVATE e7vm_static)
  target_include_directories(e7_fuzz_${t}_replay PRIVATE ${CMAKE_SOURCE_DIR}/src)
endforeach()
add_test(NAME fuzz_compile_replay COMMAND e7_fuzz_compile_replay ${CMAKE_CURRENT_SOURCE_DIR}/cases ${CMAKE_SOURCE_DIR}/examples ${CMAKE_SOURCE_DIR}/fuzz/regressions)
add_test(NAME fuzz_load_replay COMMAND e7_fuzz_load_replay ${CMAKE_CURRENT_SOURCE_DIR}/malicious)
if(E7_WITH_COMPILER)
  # crafted seeds (one per native / property) plus arbitrary bytes (test sources) as operation streams
  add_test(NAME fuzz_natives_replay COMMAND e7_fuzz_natives_replay ${CMAKE_SOURCE_DIR}/fuzz/seeds/natives
           ${CMAKE_CURRENT_SOURCE_DIR}/cases/lib)
endif()

# ---------------------------------------------------------------- stability (docs/ROADMAP.md 0.2.5, P0)
# CONTRIBUTING.md, "Stability checks". Under E7_GC_STRESS (a collection at every safepoint and every
# allocation) tests/cases/lib/gc_stress.ts is left out: 374,000 allocations with ~12,000 live objects
# would take hours, and every other program exercises the collector under stress.
# Instrumented and unoptimized builds (sanitizers, coverage, Debug) test fewer injection points, inspect
# the debugger's variables at fewer stops and make fewer soak calls, so that the sequential ctest runs of
# the sanitizer jobs stay within minutes; Release builds check every point.
if(E7_WITH_COMPILER)
  find_package(Threads REQUIRED)
  set(E7_STRESS_EXCLUDE "")
  set(E7_ALLOC_STRIDE 4000)
  set(E7_ALLOC_LIB_STRIDE 20)
  set(E7_ALLOC_DBG_STRIDE 4000)
  set(E7_INSPECT_LIMIT 5000)
  set(E7_SOAK_CALLS 100000)
  if(E7_GC_STRESS OR E7_SANITIZE OR E7_TSAN OR E7_COVERAGE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(E7_ALLOC_STRIDE 200)
    set(E7_ALLOC_LIB_STRIDE 10)
    set(E7_ALLOC_DBG_STRIDE 10)
    set(E7_INSPECT_LIMIT 1000)
    set(E7_SOAK_CALLS 20000)
    # the two programs with a million budget checks / debugger stops (they run in the Release jobs)
    set(E7_STRESS_EXCLUDE "gc_stress|ints64")
  endif()
  if(E7_GC_STRESS)
    set(E7_SOAK_CALLS 3000)
  endif()
  foreach(t alloc_fail_test equiv_test threads_test soak_test)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE e7vm_static Threads::Threads)
    target_include_directories(${t} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  endforeach()

  # allocation-failure injection: the N-th allocation / external charge fails, for every N (thinned out
  # for the library tests, whose crypto makes each run slower), with a heap consistency check each time
  file(GLOB E7_ALLOC_FAIL_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/cases/lang/*.ts ${CMAKE_SOURCE_DIR}/examples/*.ts)
  add_test(NAME alloc_fail_test COMMAND alloc_fail_test --stride-after ${E7_ALLOC_STRIDE} ${E7_ALLOC_FAIL_PROGRAMS})
  file(GLOB E7_ALLOC_FAIL_LIB ${CMAKE_CURRENT_SOURCE_DIR}/cases/lib/*.ts ${CMAKE_CURRENT_SOURCE_DIR}/cases/errors/*.ts
       ${CMAKE_CURRENT_SOURCE_DIR}/cases/modules/*.ts)
  add_test(NAME alloc_fail_lib COMMAND alloc_fail_test --stride-after ${E7_ALLOC_LIB_STRIDE} --exclude gc_stress
           ${E7_ALLOC_FAIL_LIB})
  # the same with every allocation from the N-th on failing, with a budget slice of 1, and under a
  # stepping debugger that inspects the heap at every stop (also at the stop for the memory error)
  add_test(NAME alloc_fail_persistent COMMAND alloc_fail_test --persistent --stride-after ${E7_ALLOC_STRIDE}
           ${E7_ALLOC_FAIL_PROGRAMS})
  add_test(NAME alloc_fail_slice COMMAND alloc_fail_test --slice --stride-after ${E7_ALLOC_STRIDE} ${E7_ALLOC_FAIL_PROGRAMS})
  add_test(NAME alloc_fail_debugger COMMAND alloc_fail_test --debugger --stride-after ${E7_ALLOC_DBG_STRIDE}
           ${E7_ALLOC_FAIL_PROGRAMS})

  # equivalence runs: every test program with a budget slice of 1 (suspend / resume at every check) and
  # under a debugger single-stepping through everything must print exactly what a normal run prints
  # (switch dispatch: the linux-gcc-switch-dispatch CI job runs this whole suite with E7_NO_COMPUTED_GOTO)
  file(GLOB_RECURSE E7_EQUIV_PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/cases/*.ts)
  list(FILTER E7_EQUIV_PROGRAMS EXCLUDE REGEX "/_")
  if(E7_STRESS_EXCLUDE)
    list(FILTER E7_EQUIV_PROGRAMS EXCLUDE REGEX "${E7_STRESS_EXCLUDE}")
  endif()
  add_test(NAME equiv_slice COMMAND equiv_test --modes slice ${E7_EQUIV_PROGRAMS})
  add_test(NAME equiv_step COMMAND equiv_test --modes step --inspect-limit ${E7_INSPECT_LIMIT} ${E7_EQUIV_PROGRAMS})

  # several VMs on several threads (each VM used by one thread); CI also runs it under E7_TSAN
  add_test(NAME threads_test COMMAND threads_test)

  # soak: one VM, 100,000 calls; heap after a full collection, GC statistics and process heap flat
  # (`soak_test --calls 10000000 -v` for a long run)
  add_test(NAME soak_test COMMAND soak_test --calls ${E7_SOAK_CALLS})
endif()
