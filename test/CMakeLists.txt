cmake_minimum_required(VERSION 3.14...3.22)

project(PhoxTests LANGUAGES CXX)

# ---- Options ----

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Dependencies ----

include(../cmake/CPM.cmake)

if(TEST_INSTALLED_VERSION)
  find_package(Phox REQUIRED)
else()
  CPMAddPackage(NAME Phox SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

# ---- Create binary ----

add_executable(${PROJECT_NAME} source/scene_test.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE Phox::Phox)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# enable compiler warnings
if(NOT TEST_INSTALLED_VERSION)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wpedantic -Wextra)
  elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
  endif()
endif()

# ---- Register the test ----
# Run from the build dir: the renderer writes build/rendered_triangle.ppm relative to the cwd.

enable_testing()
add_test(NAME scene_smoke COMMAND ${PROJECT_NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Phase 0: golden image + comparison helper (no dependency on the renderer).
add_executable(PhoxImageCompareTest source/image_compare_test.cpp)
target_compile_definitions(PhoxImageCompareTest PRIVATE PHOX_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/golden")
set_target_properties(PhoxImageCompareTest PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME image_compare COMMAND PhoxImageCompareTest)

# Phase 1: RHI interfaces on the null backend. These link the in-tree targets, so they are skipped
# when testing an installed Phox.
if(NOT TEST_INSTALLED_VERSION)
  add_executable(PhoxRhiNullTest source/rhi_null_test.cpp)
  target_link_libraries(PhoxRhiNullTest PRIVATE PhoxCore PhoxNull)
  set_target_properties(PhoxRhiNullTest PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(PhoxRhiNullTest PRIVATE -Wall -Wpedantic -Wextra)
  elseif(MSVC)
    target_compile_options(PhoxRhiNullTest PRIVATE /W4)
  endif()
  if(PHOX_BACKEND_VULKAN AND TARGET PhoxVulkan)
    target_compile_definitions(PhoxRhiNullTest PRIVATE PHOX_TEST_VULKAN_ENABLED)
  endif()
  add_test(NAME rhi_null COMMAND PhoxRhiNullTest)
endif()

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
  target_compile_options(${PROJECT_NAME} PRIVATE -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(${PROJECT_NAME} PRIVATE -fprofile-arcs -ftest-coverage)
endif()
