cmake_minimum_required(VERSION 3.14...3.22)

# ---- Project ----

# Note: update this to your new project's name and version
project(
  Phox
  VERSION 1.0
  LANGUAGES CXX
)

# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

# ---- Add dependencies via CPM ----
# see https://github.com/TheLartians/CPM.cmake for more info

include(cmake/CPM.cmake)

# PackageProject.cmake will be used to make our target installable
CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.8.0")

CPMAddPackage(
  NAME fmt
  GIT_TAG 10.2.1
  GITHUB_REPOSITORY fmtlib/fmt
  OPTIONS "FMT_INSTALL YES" # create an installable target
)
CPMAddPackage("gh:zeux/volk#vulkan-sdk-1.3.283.0")

set(VulkanMemoryAllocator_VERSION 3.1.0)
CPMAddPackage(
  NAME VulkanMemoryAllocator
  VERSION "${VulkanMemoryAllocator_VERSION}"
  URL "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v${VulkanMemoryAllocator_VERSION}.tar.gz"
  FIND_PACKAGE_ARGUMENTS "CONFIG"
  OPTIONS "VMA_ENABLE_INSTALL ON" "VMA_STATIC_VULKAN_FUNCTIONS OFF" "VMA_DYNAMIC_VULKAN_FUNCTIONS OFF"
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(VulkanMemoryAllocator INTERFACE -Wno-nullability-completeness)
endif()

# ---- Add source files ----

# Note: globbing sources is considered bad practice as CMake's generators may not detect new files
# automatically. Keep that in mind when changing files, or explicitly mention them here.# List header files
set(headers
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Buffer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Common.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Device.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Image.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Camera.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/RenderTarget.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Renderer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Swapchain.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Scene.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/VertexLayout.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/Buffer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/DescriptorSetLayout.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/Device.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/Image.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/Renderer.hpp
)

# List source files
set(sources
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/Buffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/Renderer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/Shader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/Scene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/Buffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/DescriptorSetLayout.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/Device.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/Image.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/Renderer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/Swapchain.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/pipeline/GraphicsPipeline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/pipeline/Pipeline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/pipeline/Shader.cpp
)

# ---- RHI core and backends ----
# PhoxCore is the backend-agnostic layer (RHI interfaces, later the render graph) and depends on
# nothing but the standard library. Each backend is its own target. The old Vulkan renderer in the
# Phox target below stays in place until it is removed in a later phase.

option(PHOX_BACKEND_VULKAN "Build the Vulkan RHI backend" ON)
option(PHOX_BACKEND_NULL "Build the null RHI backend (records commands, draws nothing)" ON)

set(PHOX_CORE_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Log.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/rhi/Backend.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/rhi/Capabilities.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/rhi/CommandList.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/rhi/Descs.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/rhi/Device.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/rhi/Formats.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/rhi/Handles.hpp
)
set(PHOX_CORE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/Log.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/rhi/Backend.cpp
)

add_library(PhoxCore STATIC ${PHOX_CORE_HEADERS} ${PHOX_CORE_SOURCES})
target_compile_features(PhoxCore PUBLIC cxx_std_20)
set_target_properties(PhoxCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(PhoxCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(PHOX_BACKEND_NULL)
  add_library(
    PhoxNull STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/null/include/phox/null/NullDevice.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/null/source/NullCommandList.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/null/source/NullDevice.cpp
  )
  target_compile_features(PhoxNull PUBLIC cxx_std_20)
  set_target_properties(PhoxNull PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(PhoxNull PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backends/null/include)
  target_link_libraries(PhoxNull PUBLIC PhoxCore)
  # Backend.cpp (in PhoxCore) is the one core file that constructs backend devices. The cycle between
  # these two static libraries is intentional: CMake supports it for static libraries.
  target_compile_definitions(PhoxCore PRIVATE PHOX_HAS_NULL)
  target_link_libraries(PhoxCore PRIVATE PhoxNull)
endif()

if(PHOX_BACKEND_VULKAN)
  add_library(
    PhoxVulkan STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/include/phox/vk/VulkanBackend.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/source/Pool.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/source/States.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/source/VkCommon.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/source/VulkanCommandList.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/source/VulkanDevice.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/source/States.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/source/VmaImpl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/source/VulkanBackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/source/VulkanCommandList.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/source/VulkanDevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/source/VulkanPipelines.cpp
  )
  target_compile_features(PhoxVulkan PUBLIC cxx_std_20)
  set_target_properties(PhoxVulkan PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(
    PhoxVulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan/source
  )
  target_link_libraries(PhoxVulkan PUBLIC PhoxCore PRIVATE volk::volk VulkanMemoryAllocator)
  target_compile_definitions(PhoxCore PRIVATE PHOX_HAS_VULKAN)
  target_link_libraries(PhoxCore PRIVATE PhoxVulkan)
endif()

foreach(rhi_target PhoxCore PhoxNull PhoxVulkan)
  if(TARGET ${rhi_target})
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
      target_compile_options(${rhi_target} PRIVATE -Wall -Wextra -Wpedantic)
      if(${rhi_target} STREQUAL PhoxVulkan)
        # `VkFoo info{VK_STRUCTURE_TYPE_FOO};` is the standard Vulkan idiom and trips this warning.
        target_compile_options(${rhi_target} PRIVATE -Wno-missing-field-initializers)
      endif()
    elseif(MSVC)
      target_compile_options(${rhi_target} PRIVATE /W4)
    endif()
  endif()
endforeach()

# ---- Create library ----

# Note: for header-only libraries change all PUBLIC flags to INTERFACE and create an interface
# target: add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME} ${headers} ${sources})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  find_path(MOLTENVK_INCLUDE_DIR vulkan/vulkan.h HINTS "/opt/homebrew/include" "/usr/local/include" "/usr/local/opt/molten-vk/include")
  find_library(MOLTENVK_LIB NAMES MoltenVK PATHS "/opt/homebrew/lib" "/usr/local/lib" "/usr/local/opt/molten-vk/lib")
endif()

# Link dependencies
# The installed vulkan/* headers include volk, VMA and glm, so consumers need their include paths.
# volk/VMA are fetched via CPM and are not part of our export set, hence BUILD_INTERFACE.
target_link_libraries(
  ${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:PhoxCore> $<BUILD_INTERFACE:volk::volk> $<BUILD_INTERFACE:VulkanMemoryAllocator>
  PRIVATE fmt::fmt
)

find_package(glm CONFIG)
if(glm_FOUND)
  target_link_libraries(${PROJECT_NAME} PUBLIC glm::glm)
endif()

# ---- Shaders ----
# Compiled to SPIR-V at build time; the renderer finds them via PHOX_SHADER_DIR (absolute path).

find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang)
if(NOT GLSLANG_VALIDATOR)
  message(FATAL_ERROR "glslangValidator not found; needed to compile shaders/*.vert|frag")
endif()

set(PHOX_SHADER_OUTPUT_DIR ${PROJECT_BINARY_DIR}/shaders)
set(PHOX_SHADER_SPV)
foreach(shader basic.vert basic.frag)
  set(spv ${PHOX_SHADER_OUTPUT_DIR}/${shader}.spv)
  add_custom_command(
    OUTPUT ${spv}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PHOX_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader} -o ${spv}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}
    COMMENT "Compiling shader ${shader}"
    VERBATIM
  )
  list(APPEND PHOX_SHADER_SPV ${spv})
endforeach()
add_custom_target(PhoxShaders ALL DEPENDS ${PHOX_SHADER_SPV})
add_dependencies(${PROJECT_NAME} PhoxShaders)
target_compile_definitions(${PROJECT_NAME} PRIVATE PHOX_SHADER_DIR="${PHOX_SHADER_OUTPUT_DIR}")

target_include_directories(
  ${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                         $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
)
# ---- Create an installable target ----
# this allows users to install and find the library via `find_package()`.

# the location where the project's version header will be placed should match the project's regular
# header paths
string(TOLOWER ${PROJECT_NAME}/version.h VERSION_HEADER_LOCATION)

packageProject(
  NAME ${PROJECT_NAME}
  VERSION ${PROJECT_VERSION}
  NAMESPACE ${PROJECT_NAME}
  BINARY_DIR ${PROJECT_BINARY_DIR}
  INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include
  INCLUDE_DESTINATION include/${PROJECT_NAME}-${PROJECT_VERSION}
  VERSION_HEADER "${VERSION_HEADER_LOCATION}"
  COMPATIBILITY SameMajorVersion
  DEPENDENCIES "fmt 9.1.0"
)
