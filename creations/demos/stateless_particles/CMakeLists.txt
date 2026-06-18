set(IR_STATELESS_PARTICLES_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_STATELESS_PARTICLES_RUNTIME_DIR})
add_executable(IRStatelessParticles main.cpp)
target_link_libraries(IRStatelessParticles PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRStatelessParticles SCRIPTS config.lua)

add_custom_target(IRStatelessParticlesRun
    COMMAND $<TARGET_FILE:IRStatelessParticles>
    DEPENDS IRStatelessParticles IRStatelessParticlesAssets
    WORKING_DIRECTORY ${IR_STATELESS_PARTICLES_RUNTIME_DIR}
    USES_TERMINAL
)
