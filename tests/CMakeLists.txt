add_executable(capi_test capi_test.cpp)
target_link_libraries(capi_test PRIVATE e7vm_static)
add_test(NAME capi_test COMMAND capi_test)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  find_program(NODE_EXECUTABLE node)
  if(NODE_EXECUTABLE AND TARGET e7)
    add_test(NAME language_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py --e7 $<TARGET_FILE:e7> --node ${NODE_EXECUTABLE})
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
  add_test(NAME fuzz_compile COMMAND fuzz_compile 1500 ${E7_FUZZ_CORPUS})
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
foreach(t load compile)
  add_executable(e7_fuzz_${t}_replay ${CMAKE_SOURCE_DIR}/fuzz/fuzz_${t}.cpp ${CMAKE_SOURCE_DIR}/fuzz/replay_main.cpp)
  target_link_libraries(e7_fuzz_${t}_replay PRIVATE e7vm_static)
  target_include_directories(e7_fuzz_${t}_replay PRIVATE ${CMAKE_SOURCE_DIR}/src)
endforeach()
add_test(NAME fuzz_compile_replay COMMAND e7_fuzz_compile_replay ${CMAKE_CURRENT_SOURCE_DIR}/cases ${CMAKE_SOURCE_DIR}/examples ${CMAKE_SOURCE_DIR}/fuzz/regressions)
add_test(NAME fuzz_load_replay COMMAND e7_fuzz_load_replay ${CMAKE_CURRENT_SOURCE_DIR}/malicious)
