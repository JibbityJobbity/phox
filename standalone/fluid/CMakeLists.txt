# The fluid sample's library: solvers, the frame feature and their compute shaders. Added by the
# standalone project (PhoxFluidSample) and the tests (PhoxFluidTests), each after it has added Phox.

find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang REQUIRED)

set(PHOX_FLUID_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(PHOX_FLUID_SHADER_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/shaders/fluid_common.glsl
                               ${CMAKE_CURRENT_LIST_DIR}/shaders/fluid_lbm_common.glsl
                               ${CMAKE_CURRENT_LIST_DIR}/shaders/fluid_sph_common.glsl)
set(PHOX_FLUID_SHADER_SPV)
foreach(
  shader
  fluid_sf_forces.comp
  fluid_sf_advect.comp
  fluid_sf_divergence.comp
  fluid_sf_jacobi.comp
  fluid_sf_project.comp
  fluid_lbm_init.comp
  fluid_lbm_step.comp
  fluid_sph_init.comp
  fluid_sph_count.comp
  fluid_sph_scan.comp
  fluid_sph_scatter.comp
  fluid_sph_sort.comp
  fluid_sph_density.comp
  fluid_sph_integrate.comp
  fluid_sph_fields.comp
  fluid_surface.comp
)
  set(spv ${PHOX_FLUID_SHADER_OUTPUT_DIR}/${shader}.spv)
  add_custom_command(
    OUTPUT ${spv}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PHOX_FLUID_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLANG_VALIDATOR} -V ${CMAKE_CURRENT_LIST_DIR}/shaders/${shader} -o ${spv}
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/shaders/${shader} ${PHOX_FLUID_SHADER_INCLUDES}
    COMMENT "Compiling fluid shader ${shader}"
    VERBATIM
  )
  list(APPEND PHOX_FLUID_SHADER_SPV ${spv})
endforeach()
add_custom_target(PhoxFluidShaders DEPENDS ${PHOX_FLUID_SHADER_SPV})

add_library(
  PhoxFluid STATIC
  ${CMAKE_CURRENT_LIST_DIR}/source/Persistent.cpp ${CMAKE_CURRENT_LIST_DIR}/source/StableFluids.cpp
  ${CMAKE_CURRENT_LIST_DIR}/source/LatticeBoltzmann.cpp ${CMAKE_CURRENT_LIST_DIR}/source/SmoothedParticles.cpp
  ${CMAKE_CURRENT_LIST_DIR}/source/FluidFeature.cpp
)
add_dependencies(PhoxFluid PhoxFluidShaders)
target_include_directories(PhoxFluid PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(PhoxFluid PUBLIC Phox::Phox)
target_compile_definitions(PhoxFluid PRIVATE PHOX_FLUID_SHADER_DIR="${PHOX_FLUID_SHADER_OUTPUT_DIR}")
set_target_properties(PhoxFluid PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(PhoxFluid PRIVATE -Wall -Wpedantic -Wextra)
endif()
