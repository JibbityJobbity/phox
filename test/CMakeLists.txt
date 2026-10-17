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

  # Phase 3: the render graph, on the null backend (no GPU needed).
  add_executable(PhoxGraphTest source/graph_test.cpp)
  target_link_libraries(PhoxGraphTest PRIVATE PhoxCore PhoxNull)
  set_target_properties(PhoxGraphTest PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(PhoxGraphTest PRIVATE -Wall -Wpedantic -Wextra)
  elseif(MSVC)
    target_compile_options(PhoxGraphTest PRIVATE /W4)
  endif()
  add_test(NAME graph COMMAND PhoxGraphTest)

  # Phase 4: scene/camera, and the forward technique recorded on the null backend (no GPU needed).
  add_executable(PhoxCameraTest source/camera_test.cpp)
  target_link_libraries(PhoxCameraTest PRIVATE PhoxScene)
  add_test(NAME camera COMMAND PhoxCameraTest)

  add_executable(PhoxForwardGraphTest source/forward_graph_test.cpp)
  target_link_libraries(PhoxForwardGraphTest PRIVATE PhoxTechniques PhoxScene PhoxCore PhoxNull)
  add_test(NAME forward_graph COMMAND PhoxForwardGraphTest)

  foreach(phase4_test PhoxCameraTest PhoxForwardGraphTest)
    set_target_properties(${phase4_test} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
      target_compile_options(${phase4_test} PRIVATE -Wall -Wpedantic -Wextra)
    elseif(MSVC)
      target_compile_options(${phase4_test} PRIVATE /W4)
    endif()
  endforeach()

  # Phase 2: the same frame through the Vulkan backend, compared against the golden image.
  if(PHOX_BACKEND_VULKAN AND TARGET PhoxVulkan)
    find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang REQUIRED)
    get_target_property(PHOX_SOURCE_BINARY_DIR Phox BINARY_DIR)
    set(PHOX_TEST_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    set(PHOX_TEST_SHADER_SPV)
    foreach(shader two_sets.vert two_sets.frag)
      set(spv ${PHOX_TEST_SHADER_OUTPUT_DIR}/${shader}.spv)
      add_custom_command(
        OUTPUT ${spv}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PHOX_TEST_SHADER_OUTPUT_DIR}
        COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_LIST_DIR}/shaders/${shader} -o ${spv}
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/shaders/${shader}
        COMMENT "Compiling test shader ${shader}"
        VERBATIM
      )
      list(APPEND PHOX_TEST_SHADER_SPV ${spv})
    endforeach()
    add_custom_target(PhoxTestShaders ALL DEPENDS ${PHOX_TEST_SHADER_SPV})

    add_executable(PhoxRhiTriangleTest source/rhi_triangle_test.cpp)
    target_link_libraries(PhoxRhiTriangleTest PRIVATE PhoxCore PhoxVulkan)
    add_dependencies(PhoxRhiTriangleTest PhoxShaders PhoxTestShaders)
    target_compile_definitions(
      PhoxRhiTriangleTest
      PRIVATE PHOX_SHADER_DIR="${PHOX_SOURCE_BINARY_DIR}/shaders"
              PHOX_TEST_SHADER_DIR="${PHOX_TEST_SHADER_OUTPUT_DIR}"
              PHOX_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/golden"
    )
    set_target_properties(PhoxRhiTriangleTest PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
      target_compile_options(PhoxRhiTriangleTest PRIVATE -Wall -Wpedantic -Wextra)
    elseif(MSVC)
      target_compile_options(PhoxRhiTriangleTest PRIVATE /W4)
    endif()
    add_test(NAME rhi_triangle COMMAND PhoxRhiTriangleTest)

    # Phase 4: the baseline frame through the forward technique and the render graph.
    add_executable(PhoxForwardTest source/forward_test.cpp)
    target_link_libraries(PhoxForwardTest PRIVATE PhoxTechniques PhoxScene PhoxCore)
    target_compile_definitions(PhoxForwardTest PRIVATE PHOX_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/golden")
    set_target_properties(PhoxForwardTest PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
      target_compile_options(PhoxForwardTest PRIVATE -Wall -Wpedantic -Wextra)
    elseif(MSVC)
      target_compile_options(PhoxForwardTest PRIVATE /W4)
    endif()
    add_test(NAME forward COMMAND PhoxForwardTest)
    set_tests_properties(rhi_triangle forward PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
  target_compile_options(${PROJECT_NAME} PRIVATE -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(${PROJECT_NAME} PRIVATE -fprofile-arcs -ftest-coverage)
endif()
