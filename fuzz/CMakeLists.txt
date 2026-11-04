# libFuzzer targets (clang). Two ways to build:
#   local:        cmake -S . -B build-fuzz -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
#                       -DE7_FUZZ=ON -DE7_SANITIZE=ON -DE7_BUILD_TESTS=OFF && cmake --build build-fuzz
#                 build-fuzz/fuzz/e7_fuzz_compile -max_total_time=60 fuzz/corpus/compile
#   OSS-Fuzz / ClusterFuzzLite: CFLAGS/CXXFLAGS and $LIB_FUZZING_ENGINE come from the environment
#                 (see .clusterfuzzlite/build.sh).
set(E7_FUZZ_ENGINE "$ENV{LIB_FUZZING_ENGINE}")
foreach(t load compile)
  add_executable(e7_fuzz_${t} fuzz_${t}.cpp)
  target_link_libraries(e7_fuzz_${t} PRIVATE e7vm_static)
  target_include_directories(e7_fuzz_${t} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  if(E7_FUZZ_ENGINE)
    target_link_options(e7_fuzz_${t} PRIVATE ${E7_FUZZ_ENGINE})
  else()
    target_compile_options(e7_fuzz_${t} PRIVATE -fsanitize=fuzzer)
    target_link_options(e7_fuzz_${t} PRIVATE -fsanitize=fuzzer)
  endif()
endforeach()
