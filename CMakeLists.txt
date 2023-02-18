add_library(phox STATIC)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED glslang)
find_package(glfw3 REQUIRED)

add_subdirectory(thirdparty)

target_sources( phox
    PRIVATE
        src/phox.cpp
        src/Renderer.cpp
        src/Device.cpp
)

target_include_directories( phox
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${glfw_INCLUDE_DIR}
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries( phox
    PUBLIC
        volk
)
