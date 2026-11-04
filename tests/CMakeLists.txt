add_executable(capi_test capi_test.cpp)
target_link_libraries(capi_test PRIVATE e7vm)
add_test(NAME capi_test COMMAND capi_test)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME language_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py --e7 $<TARGET_FILE:e7>)
endif()

add_executable(fuzz_load fuzz_load.cpp)
target_link_libraries(fuzz_load PRIVATE e7vm)
target_include_directories(fuzz_load PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME fuzz_load COMMAND fuzz_load 3000)
