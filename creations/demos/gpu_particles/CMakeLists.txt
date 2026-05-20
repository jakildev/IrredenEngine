set(IR_GPU_PARTICLES_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_GPU_PARTICLES_RUNTIME_DIR})
add_executable(IRGpuParticles main.cpp)
target_link_libraries(IRGpuParticles PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRGpuParticles
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRGpuParticles> $<TARGET_RUNTIME_DLLS:IRGpuParticles>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_GPU_PARTICLES_SCRIPTS_DIR ${IR_GPU_PARTICLES_RUNTIME_DIR}/scripts)
set(IR_GPU_PARTICLES_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_GPU_PARTICLES_CONFIG_LUA_DST ${IR_GPU_PARTICLES_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_GPU_PARTICLES_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_GPU_PARTICLES_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_GPU_PARTICLES_CONFIG_LUA_SRC} ${IR_GPU_PARTICLES_CONFIG_LUA_DST}
    DEPENDS ${IR_GPU_PARTICLES_CONFIG_LUA_SRC}
    COMMENT "Syncing gpu_particles config.lua"
)

add_custom_target(IRGpuParticlesAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_GPU_PARTICLES_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_GPU_PARTICLES_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_GPU_PARTICLES_RUNTIME_DIR}/shaders
    DEPENDS ${IR_GPU_PARTICLES_CONFIG_LUA_DST}
)

add_dependencies(IRGpuParticles IRGpuParticlesAssets)

add_custom_target(IRGpuParticlesRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_GPU_PARTICLES_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_GPU_PARTICLES_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_GPU_PARTICLES_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_GPU_PARTICLES_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRGpuParticles>
    DEPENDS IRGpuParticles
    WORKING_DIRECTORY ${IR_GPU_PARTICLES_RUNTIME_DIR}
    USES_TERMINAL
)
