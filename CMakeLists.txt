cmake_minimum_required(VERSION 3.10)
add_library(phox STATIC)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(VMA_STATIC_VULKAN_FUNCTIONS OFF)
set(VMA_DYNAMIC_VULKAN_FUNCTIONS OFF)

find_package(Vulkan COMPONENTS glslc volk)
find_package(glfw3)

add_subdirectory(thirdparty)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CXX_FLAGS "-Wall")

target_sources( phox
    PRIVATE
        src/phox.cpp
        src/Renderer.cpp
        src/Device.cpp
        src/Buffer.cpp
        src/Model.cpp
        src/Image.cpp
        src/Pipeline/Pipeline.cpp
        src/Pipeline/GraphicsPipeline.cpp
        src/Pipeline/PipelineLayout.cpp
        src/Pipeline/Shader.cpp
)

# print the current source dir
message(STATUS "Current source dir: ${CMAKE_CURRENT_SOURCE_DIR}")

target_include_directories( phox
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
    ${glfw_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${ASSIMP_INCLUDE_DIRS}
    )


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# export thirdparty include dirs


target_link_libraries( phox
    PUBLIC
        Vulkan::volk
        VulkanMemoryAllocator
        assimp
)
