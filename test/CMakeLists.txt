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

# Pure graph logic: no GPU needed.
add_executable(PhoxGraphTests source/render_graph_test.cpp)
target_link_libraries(PhoxGraphTests PRIVATE Phox::Phox)
set_target_properties(PhoxGraphTests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
if(NOT TEST_INSTALLED_VERSION)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(PhoxGraphTests PRIVATE -Wall -Wpedantic -Wextra)
  elseif(MSVC)
    target_compile_options(PhoxGraphTests PRIVATE /W4)
  endif()
endif()

# ---- Register the test ----
# Run from the build dir: the renderer writes build/rendered_triangle.ppm relative to the cwd.

enable_testing()
add_test(NAME scene_smoke COMMAND ${PROJECT_NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME render_graph COMMAND PhoxGraphTests)

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
  target_compile_options(${PROJECT_NAME} PRIVATE -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(${PROJECT_NAME} PRIVATE -fprofile-arcs -ftest-coverage)
endif()
