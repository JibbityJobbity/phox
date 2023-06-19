add_library(phox STATIC include/Buffer.hpp src/Buffer.cpp)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED glslang)
find_package(glfw3 REQUIRED)

add_subdirectory(thirdparty)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

target_sources( phox
    PRIVATE
        src/phox.cpp
        src/Renderer.cpp
        src/Device.cpp
        src/Buffer.cpp
        src/Model.cpp
        src/Image.cpp
)

# print the current source dir
message(STATUS "Current source dir: ${CMAKE_CURRENT_SOURCE_DIR}")

target_include_directories( phox
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/volk
    PRIVATE
    ${glfw_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
    ${ASSIMP_INCLUDE_DIRS}
)

# export thirdparty include dirs


target_link_libraries( phox
    PUBLIC
        volk
        VulkanMemoryAllocator
        assimp
)
