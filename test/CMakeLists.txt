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

# ---- Create binaries ----

function(phox_add_test_executable name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE Phox::Phox)
  set_target_properties(${name} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
  if(NOT TEST_INSTALLED_VERSION)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
      target_compile_options(${name} PRIVATE -Wall -Wpedantic -Wextra)
    elseif(MSVC)
      target_compile_options(${name} PRIVATE /W4)
    endif()
  endif()
  if(ENABLE_TEST_COVERAGE)
    target_compile_options(${name} PRIVATE -O0 -g -fprofile-arcs -ftest-coverage)
    target_link_options(${name} PRIVATE -fprofile-arcs -ftest-coverage)
  endif()
endfunction()

phox_add_test_executable(${PROJECT_NAME} source/scene_test.cpp)
# Pure graph logic: no GPU needed.
phox_add_test_executable(PhoxGraphTests source/render_graph_test.cpp)
# Scene, camera, view extraction, software rasterizer, mesh loading: no GPU needed.
phox_add_test_executable(PhoxViewTests source/render_view_test.cpp)
# Renders headless and compares with test/golden/*.ppm (see the comment at the top of the source).
phox_add_test_executable(PhoxGoldenTests source/golden_test.cpp)
target_compile_definitions(PhoxGoldenTests PRIVATE PHOX_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

# ---- Register the tests ----

enable_testing()
add_test(NAME scene_smoke COMMAND ${PROJECT_NAME})
add_test(NAME render_graph COMMAND PhoxGraphTests)
add_test(NAME render_view COMMAND PhoxViewTests)
add_test(NAME golden COMMAND PhoxGoldenTests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
# 77: no Vulkan device (set PHOX_REQUIRE_GPU=1 to make that a failure instead).
set_tests_properties(golden PROPERTIES SKIP_RETURN_CODE 77)
