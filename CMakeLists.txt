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

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
  add_compile_options(/W3 /EHsc /utf-8 /D_CRT_SECURE_NO_WARNINGS)
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
# portable, UB-free byte access on every architecture
target_compile_definitions(e7_bearssl PRIVATE BR_LE_UNALIGNED=0 BR_BE_UNALIGNED=0)
if(NOT MSVC)
  target_compile_options(e7_bearssl PRIVATE -w)
endif()

add_library(e7_monocypher STATIC third_party/monocypher/monocypher.c)
target_include_directories(e7_monocypher PUBLIC third_party/monocypher)
if(NOT MSVC)
  target_compile_options(e7_monocypher PRIVATE -w)
endif()

# ---------------------------------------------------------------- VM (runtime)
set(E7_VM_SRC
  src/core/utf.cpp src/core/numfmt.cpp src/core/serialize.cpp src/core/verify.cpp src/core/disasm.cpp
  src/vm/vm.cpp src/vm/interp.cpp
  src/stdlib/core.cpp src/stdlib/string.cpp src/stdlib/array.cpp src/stdlib/map.cpp src/stdlib/math.cpp
  src/stdlib/bytes.cpp src/stdlib/bigint.cpp src/stdlib/json.cpp src/stdlib/encoding.cpp
  src/crypto/crypto.cpp src/crypto/keccak.cpp src/crypto/sign.cpp
  src/capi/capi.cpp)
set(E7_COMPILER_SRC
  src/compiler/parser.cpp src/compiler/types.cpp src/compiler/checker.cpp src/compiler/checker_stmt.cpp
  src/compiler/checker_expr.cpp src/compiler/checker_builtins.cpp src/compiler/checker_ns.cpp
  src/compiler/codegen.cpp src/compiler/compiler.cpp)

if(E7_BUILD_SHARED)
  add_library(e7vm SHARED ${E7_VM_SRC})
else()
  add_library(e7vm STATIC ${E7_VM_SRC})
endif()
target_include_directories(e7vm PUBLIC include PRIVATE src)
target_link_libraries(e7vm PRIVATE e7_bearssl e7_monocypher)
if(WIN32)
  target_link_libraries(e7vm PRIVATE advapi32)
endif()
if(E7_NO_COMPUTED_GOTO)
  target_compile_definitions(e7vm PRIVATE E7_NO_COMPUTED_GOTO=1)
endif()

if(E7_WITH_COMPILER)
  add_library(e7c_lib STATIC ${E7_COMPILER_SRC})
  target_include_directories(e7c_lib PUBLIC include PRIVATE src)
  target_link_libraries(e7vm PUBLIC e7c_lib)
  target_compile_definitions(e7vm PUBLIC E7_WITH_COMPILER=1)
endif()

# ---------------------------------------------------------------- tools
if(E7_BUILD_TOOLS)
  add_executable(e7 tools/e7.cpp)
  target_link_libraries(e7 PRIVATE e7vm)
  target_include_directories(e7 PRIVATE src)
  add_executable(e7c tools/e7c.cpp)
  target_link_libraries(e7c PRIVATE e7vm)
  target_include_directories(e7c PRIVATE src)
endif()

if(E7_BUILD_JNI)
  add_library(e7vm_jni SHARED platform/android/jni/e7_jni.cpp)
  target_link_libraries(e7vm_jni PRIVATE e7vm)
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

install(TARGETS e7vm ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
install(FILES include/e7/e7.h DESTINATION include/e7)
