cmake_minimum_required(VERSION 3.14...3.22)

project(PhoxSample LANGUAGES CXX)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Dependencies ----

include(../cmake/CPM.cmake)

CPMAddPackage(
  GITHUB_REPOSITORY jarro2783/cxxopts
  VERSION 3.0.0
  OPTIONS "CXXOPTS_BUILD_EXAMPLES NO" "CXXOPTS_BUILD_TESTS NO" "CXXOPTS_ENABLE_INSTALL YES"
)

CPMAddPackage(NAME Phox SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

# ---- Create standalone executable ----

file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)

add_executable(${PROJECT_NAME} ${sources})

target_link_libraries(${PROJECT_NAME} Phox::Phox cxxopts::cxxopts)

# The same baseline frame through the RHI stack: forward technique, render graph, RHI Vulkan backend.
add_executable(PhoxRhiSample ${CMAKE_CURRENT_SOURCE_DIR}/rhi/main.cpp)
target_link_libraries(PhoxRhiSample PRIVATE PhoxTechniques PhoxScene PhoxCore cxxopts::cxxopts)

# The fluid sample: swappable 2D fluid solvers on the async compute queue, drawn as a GPU-made surface.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/fluid ${CMAKE_CURRENT_BINARY_DIR}/fluid)
add_executable(PhoxFluidSample ${CMAKE_CURRENT_SOURCE_DIR}/fluid/main.cpp)
target_link_libraries(PhoxFluidSample PRIVATE PhoxFluid cxxopts::cxxopts)
set_target_properties(PhoxFluidSample PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
