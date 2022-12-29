add_library(phox STATIC)

find_package(Vulkan REQUIRED glslang)

target_sources( phox
    PRIVATE
        src/phox.cpp
)

target_include_directories( phox
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    Vulkan_INCLUDE_DIRS
)
