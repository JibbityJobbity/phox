cmake_minimum_required(VERSION 3.14...3.22)

# ---- Project ----

# Note: update this to your new project's name and version
project(
  Phox
  VERSION 1.0
  LANGUAGES CXX
)

set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_OBJ_IMPORTER ON)
# Newer compilers (e.g. clang 21's -Wnontrivial-memcall) warn in assimp 5.4.3, which builds with -Werror.
set(ASSIMP_WARNINGS_AS_ERRORS OFF)

# The Metal backend (Objective-C++), alongside Vulkan. Defaults to on where Metal exists.
option(PHOX_ENABLE_METAL "Build the Metal backend (Apple platforms only)" ${APPLE})
if(PHOX_ENABLE_METAL)
  if(NOT APPLE)
    message(FATAL_ERROR "PHOX_ENABLE_METAL needs an Apple platform")
  endif()
  enable_language(OBJCXX)
endif()

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
  target_compile_options(VulkanMemoryAllocator INTERFACE -Wno-nullability-completeness -Wno-nullability-extension)
endif()

# Math types (phox/Math.hpp). Uses an installed glm when there is one.
CPMFindPackage(
  NAME glm
  GITHUB_REPOSITORY g-truc/glm
  GIT_TAG 1.0.1
)

# Meshlets and vertex cache ordering (phox/Meshlets.hpp).
CPMAddPackage("gh:zeux/meshoptimizer@0.22")

CPMAddPackage(
  NAME assimp
  GITHUB_REPOSITORY assimp/assimp
  VERSION "5.4.3"
)

# ---- Add source files ----

# Note: globbing sources is considered bad practice as CMake's generators may not detect new files
# automatically. Keep that in mind when changing files, or explicitly mention them here.# List header files
set(headers
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Buffer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Camera.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/CpuImage.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Device.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Image.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Log.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Math.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Meshlets.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/MeshLoader.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/RenderGraph.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/RenderView.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Renderer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Resources.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/SampleScenes.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Scene.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/ShaderInterface.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/SoftwareRasterizer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/VertexLayout.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/Window.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/pipeline/Pipeline.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/pipeline/PipelineDesc.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/pipeline/Shader.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/platform/Platform.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/platform/PlatformLinux.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/platform/PlatformMacOS.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/platform/PlatformWindow.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/platform/SDLWindow.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/Buffer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/Conversions.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/DescriptorAllocator.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/Device.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/FrameContext.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/GraphExecutor.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/Image.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/MeshCache.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/MeshletVisibility.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/PassTimer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/PipelineCache.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/Renderer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/ShaderInterface.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/ShaderLibrary.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/Swapchain.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/TransientPool.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/UploadArena.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/VulkanCommon.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/passes/DepthPyramidPass.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/passes/ForwardPass.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/passes/ReadbackPass.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/passes/TonemapPass.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/phox.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/vulkan/pipeline/Shader.hpp
)

# List source files
set(sources
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/Camera.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/CpuImage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/Log.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/MeshLoader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/Meshlets.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/PipelineDesc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/RenderGraph.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/RenderView.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/Renderer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/SampleScenes.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/Scene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/Shader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/core/SoftwareRasterizer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/platform/window/SDLWindow.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/platform/window/Window.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/Buffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/Conversions.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/DescriptorAllocator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/Device.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/FrameContext.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/GraphExecutor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/Image.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/MeshCache.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/MeshletVisibility.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/PassTimer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/PipelineCache.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/Renderer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/ShaderLibrary.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/Swapchain.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/TransientPool.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/UploadArena.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/passes/DepthPyramidPass.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/passes/ForwardPass.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/passes/ReadbackPass.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/passes/TonemapPass.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/vulkan/pipeline/Shader.cpp
)

if(PHOX_ENABLE_METAL)
  list(APPEND headers
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/Buffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/Conversions.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/Device.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/GraphExecutor.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/Image.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/MeshCache.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/MetalCommon.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/PipelineCache.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/Renderer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/ShaderInterface.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/ShaderLibrary.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/TransientPool.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/passes/ForwardPass.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/passes/ReadbackPass.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/phox/metal/passes/TonemapPass.hpp
  )
  set(metal_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/Buffer.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/Conversions.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/Device.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/GraphExecutor.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/Image.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/MeshCache.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/PipelineCache.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/Renderer.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/ShaderLibrary.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/TransientPool.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/passes/ForwardPass.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/passes/ReadbackPass.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/source/phox/metal/passes/TonemapPass.mm
  )
  set_source_files_properties(${metal_sources} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  list(APPEND sources ${metal_sources})
endif()

# ---- Create library ----

# Note: for header-only libraries change all PUBLIC flags to INTERFACE and create an interface
# target: add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME} ${headers} ${sources})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
# Public headers use C++20 (defaulted comparisons, etc.), so consumers need it too.
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${PROJECT_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")
# Warnings for Phox's own sources only (dependencies are built separately).
target_compile_options(
  ${PROJECT_NAME} PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow>
                          $<$<COMPILE_LANG_AND_ID:OBJCXX,Clang,AppleClang>:-Wall -Wextra -Wshadow>
                          $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  find_path(MOLTENVK_INCLUDE_DIR vulkan/vulkan.h HINTS "/opt/homebrew/include" "/usr/local/include" "/usr/local/opt/molten-vk/include")
  find_library(MOLTENVK_LIB NAMES MoltenVK PATHS "/opt/homebrew/lib" "/usr/local/lib" "/usr/local/opt/molten-vk/lib")
endif()

# Link dependencies
# The installed vulkan/* headers include volk, VMA and glm, so consumers need their include paths.
# volk/VMA are fetched via CPM and are not part of our export set, hence BUILD_INTERFACE.
target_link_libraries(
  ${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:volk::volk> $<BUILD_INTERFACE:VulkanMemoryAllocator> fmt::fmt
  PRIVATE assimp meshoptimizer
)

find_package(SDL2 REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)

if(PHOX_ENABLE_METAL)
  target_compile_definitions(${PROJECT_NAME} PRIVATE PHOX_HAS_METAL=1)
  target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Foundation")
endif()

# Public headers use glm (phox/Math.hpp); everyone must agree on its configuration.
target_link_libraries(${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:glm::glm>)
target_compile_definitions(${PROJECT_NAME} PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)

# ---- Shaders ----
# GLSL is compiled to SPIR-V at build time; the renderer finds it via PHOX_SHADER_DIR (absolute path).
# The Metal backend compiles shaders/metal/*.metal at runtime (an offline `metal` compiler needs Xcode's
# Metal Toolchain), so those are copied to PHOX_SHADER_DIR/metal.

find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang)
if(NOT GLSLANG_VALIDATOR)
  message(FATAL_ERROR "glslangValidator not found; needed to compile shaders/*")
endif()

set(PHOX_SHADER_OUTPUT_DIR ${PROJECT_BINARY_DIR}/shaders)
set(PHOX_SHADER_SPV)
set(PHOX_SHADER_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common.glsl
                         ${CMAKE_CURRENT_SOURCE_DIR}/shaders/forward_task.glsl
)
foreach(
  shader
  forward.vert
  forward.frag
  forward.task
  forward_early.task
  forward_late.task
  forward.mesh
  fullscreen.vert
  tonemap.frag
  depthpyramid.comp
)
  set(spv ${PHOX_SHADER_OUTPUT_DIR}/${shader}.spv)
  # Task and mesh shaders (VK_EXT_mesh_shader) need SPIR-V 1.4 or later.
  set(target_env)
  if(shader MATCHES "\\.(task|mesh)$")
    set(target_env --target-env vulkan1.3)
  endif()
  add_custom_command(
    OUTPUT ${spv}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PHOX_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLANG_VALIDATOR} -V ${target_env} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader} -o ${spv}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader} ${PHOX_SHADER_INCLUDES}
    COMMENT "Compiling shader ${shader}"
    VERBATIM
  )
  list(APPEND PHOX_SHADER_SPV ${spv})
endforeach()
if(PHOX_ENABLE_METAL)
  foreach(shader common.h forward.metal fullscreen.metal tonemap.metal)
    set(copy ${PHOX_SHADER_OUTPUT_DIR}/metal/${shader})
    add_custom_command(
      OUTPUT ${copy}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/${shader} ${copy}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/${shader}
      COMMENT "Copying Metal shader ${shader}"
      VERBATIM
    )
    list(APPEND PHOX_SHADER_SPV ${copy})
  endforeach()
endif()
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
