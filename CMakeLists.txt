cmake_minimum_required(VERSION 3.16)
project(e7vm VERSION 1.0.0 LANGUAGES C CXX)

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

if(E7_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
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

add_library(e7_monocypher STATIC third_party/monocypher/monocypher.c)
target_include_directories(e7_monocypher PUBLIC third_party/monocypher)
set_target_properties(e7_monocypher PROPERTIES C_VISIBILITY_PRESET hidden)
if(NOT MSVC)
  target_compile_options(e7_monocypher PRIVATE -w)
endif()

# ---------------------------------------------------------------- core / compiler / VM
set(E7_CORE_SRC src/core/utf.cpp src/core/numfmt.cpp src/core/serialize.cpp src/core/verify.cpp src/core/disasm.cpp)
set(E7_VM_SRC
  src/vm/vm.cpp src/vm/interp.cpp
  src/stdlib/core.cpp src/stdlib/string.cpp src/stdlib/array.cpp src/stdlib/map.cpp src/stdlib/math.cpp
  src/stdlib/bytes.cpp src/stdlib/bigint.cpp src/stdlib/json.cpp src/stdlib/encoding.cpp src/stdlib/date.cpp
  src/stdlib/test.cpp
  src/crypto/crypto.cpp src/crypto/keccak.cpp src/crypto/sign.cpp
  src/capi/capi.cpp)
set(E7_COMPILER_SRC
  src/compiler/parser.cpp src/compiler/types.cpp src/compiler/checker.cpp src/compiler/checker_stmt.cpp
  src/compiler/checker_expr.cpp src/compiler/checker_builtins.cpp src/compiler/checker_ns.cpp
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
  add_executable(e7 tools/e7.cpp tools/e7_test.cpp)
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
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString
    -sEXPORTED_FUNCTIONS=_e7w_run,_e7w_output,_e7w_result,_e7w_error,_e7w_bytecode_size,_e7w_version,_e7w_disasm,_malloc,_free)
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
