cmake_minimum_required(VERSION 3.16)
project(lob LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LOB_NATIVE     "Compile with -march=native (faster, not portable)" OFF)
option(LOB_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LOB_BUILD_BENCHMARKS "Build benchmark executables" ON)
option(LOB_BUILD_TESTS      "Build tests" ON)

# Header-only core library.
add_library(lob INTERFACE)
target_include_directories(lob INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(lob INTERFACE cxx_std_20)

add_library(lob_warnings INTERFACE)
target_compile_options(lob_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
if(LOB_NATIVE)
  target_compile_options(lob_warnings INTERFACE -march=native)
endif()
if(LOB_SANITIZERS)
  target_compile_options(lob_warnings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(lob_warnings INTERFACE -fsanitize=address,undefined)
endif()

function(lob_executable name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE lob lob_warnings)
endfunction()

# Applications
lob_executable(demo         src/demo.cpp)
lob_executable(replay       src/replay.cpp)
lob_executable(gen_workload tools/gen_workload.cpp)

# Tests
if(LOB_BUILD_TESTS)
  enable_testing()
  foreach(t order_index_test order_book_test matching_engine_test differential_test)
    lob_executable(${t} tests/${t}.cpp)
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()

# Benchmarks
if(LOB_BUILD_BENCHMARKS)
  foreach(b throughput_benchmark latency_benchmark cancel_benchmark order_index_benchmark)
    lob_executable(${b} benchmarks/${b}.cpp)
    target_include_directories(${b} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks)
  endforeach()
endif()
