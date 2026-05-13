set(IR_STATELESS_PARTICLES_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_STATELESS_PARTICLES_RUNTIME_DIR})
add_executable(IRStatelessParticles main.cpp)
target_link_libraries(IRStatelessParticles PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRStatelessParticles
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRStatelessParticles> $<TARGET_RUNTIME_DLLS:IRStatelessParticles>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_STATELESS_PARTICLES_SCRIPTS_DIR ${IR_STATELESS_PARTICLES_RUNTIME_DIR}/scripts)
set(IR_STATELESS_PARTICLES_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_STATELESS_PARTICLES_CONFIG_LUA_DST ${IR_STATELESS_PARTICLES_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_STATELESS_PARTICLES_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_STATELESS_PARTICLES_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_STATELESS_PARTICLES_CONFIG_LUA_SRC} ${IR_STATELESS_PARTICLES_CONFIG_LUA_DST}
    DEPENDS ${IR_STATELESS_PARTICLES_CONFIG_LUA_SRC}
    COMMENT "Syncing stateless_particles config.lua"
)

add_custom_target(IRStatelessParticlesAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_STATELESS_PARTICLES_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_STATELESS_PARTICLES_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_STATELESS_PARTICLES_RUNTIME_DIR}/shaders
    DEPENDS ${IR_STATELESS_PARTICLES_CONFIG_LUA_DST}
)

add_dependencies(IRStatelessParticles IRStatelessParticlesAssets)

add_custom_target(IRStatelessParticlesRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_STATELESS_PARTICLES_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_STATELESS_PARTICLES_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_STATELESS_PARTICLES_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_STATELESS_PARTICLES_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRStatelessParticles>
    DEPENDS IRStatelessParticles
    WORKING_DIRECTORY ${IR_STATELESS_PARTICLES_RUNTIME_DIR}
    USES_TERMINAL
)
