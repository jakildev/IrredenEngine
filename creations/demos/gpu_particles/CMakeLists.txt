set(IR_GPU_PARTICLES_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_GPU_PARTICLES_RUNTIME_DIR})
add_executable(IRGpuParticles main.cpp)
target_link_libraries(IRGpuParticles PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRGpuParticles SCRIPTS config.lua)

add_custom_target(IRGpuParticlesRun
    COMMAND $<TARGET_FILE:IRGpuParticles>
    DEPENDS IRGpuParticles IRGpuParticlesAssets
    WORKING_DIRECTORY ${IR_GPU_PARTICLES_RUNTIME_DIR}
    USES_TERMINAL
)
