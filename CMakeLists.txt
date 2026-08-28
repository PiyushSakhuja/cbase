cmake_minimum_required(VERSION 3.16)
project(cbase CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(cbase_storage STATIC
  storage/page.cpp
  storage/disk_manager.cpp
  storage/buffer_pool.cpp
  storage/heap_file.cpp)
target_include_directories(cbase_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cbase_storage PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Interactive CLI (1. Insert / 2. Delete / 3. Scan / 4. Exit)
add_executable(cbase main.cpp)
target_link_libraries(cbase PRIVATE cbase_storage)

# Guided walkthrough with real buffer pool / persistence metrics
add_executable(cbase_demo demo.cpp)
target_link_libraries(cbase_demo PRIVATE cbase_storage)

# Timed insert / scan / reopen benchmark (no fake numbers)
add_executable(cbase_bench benchmark.cpp)
target_link_libraries(cbase_bench PRIVATE cbase_storage)

# --- tests ---------------------------------------------------------------
enable_testing()

foreach(suite page disk_manager buffer_pool heap_file integration)
  add_executable(${suite}_test tests/${suite}_test.cpp)
  target_link_libraries(${suite}_test PRIVATE cbase_storage)
  add_test(NAME ${suite}_test COMMAND ${suite}_test)
endforeach()
