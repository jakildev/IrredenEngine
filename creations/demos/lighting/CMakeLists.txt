set(IR_LIGHTING_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_LIGHTING_RUNTIME_DIR})

set(IR_LIGHTING_DEMO_TARGETS
    IRLightingAO
    IRLightingSunShadow
    IRLightingEmissive
    IRLightingCombined
    IRLightingDebugOverlays
    IRLightingPoint
    IRLightingSpot
    IRLightingDirectional
    IRLightingSunShading
    IRLightingSdfCascade
    IRLightingSunOrbit
    IRLightingSunElevationOrbit
)

set(IR_LIGHTING_DEMO_SOURCES
    main_ao.cpp
    main_sun_shadow.cpp
    main_emissive.cpp
    main_combined.cpp
    main_debug_overlays.cpp
    main_point.cpp
    main_spot.cpp
    main_directional.cpp
    main_sun_shading.cpp
    main_sdf_cascade.cpp
    main_sun_orbit.cpp
    main_sun_elevation_orbit.cpp
)

list(LENGTH IR_LIGHTING_DEMO_TARGETS IR_LIGHTING_DEMO_COUNT)
math(EXPR IR_LIGHTING_DEMO_LAST "${IR_LIGHTING_DEMO_COUNT} - 1")

foreach(IR_LIGHTING_DEMO_INDEX RANGE ${IR_LIGHTING_DEMO_LAST})
    list(GET IR_LIGHTING_DEMO_TARGETS ${IR_LIGHTING_DEMO_INDEX} IR_LIGHTING_DEMO_TARGET)
    list(GET IR_LIGHTING_DEMO_SOURCES ${IR_LIGHTING_DEMO_INDEX} IR_LIGHTING_DEMO_SOURCE)

    add_executable(${IR_LIGHTING_DEMO_TARGET} ${IR_LIGHTING_DEMO_SOURCE})
    target_link_libraries(${IR_LIGHTING_DEMO_TARGET} PUBLIC IrredenEngine)
    target_include_directories(${IR_LIGHTING_DEMO_TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    if(WIN32)
        add_custom_command(
            TARGET ${IR_LIGHTING_DEMO_TARGET}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy -t
                $<TARGET_FILE_DIR:${IR_LIGHTING_DEMO_TARGET}>
                $<TARGET_RUNTIME_DLLS:${IR_LIGHTING_DEMO_TARGET}>
            COMMAND_EXPAND_LISTS
        )
    endif()
endforeach()

set(IR_LIGHTING_SCRIPTS_DIR ${IR_LIGHTING_RUNTIME_DIR}/scripts)
set(IR_LIGHTING_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_LIGHTING_CONFIG_LUA_DST ${IR_LIGHTING_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_LIGHTING_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_LIGHTING_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_LIGHTING_CONFIG_LUA_SRC} ${IR_LIGHTING_CONFIG_LUA_DST}
    DEPENDS ${IR_LIGHTING_CONFIG_LUA_SRC}
    COMMENT "Syncing lighting demo config.lua"
)

add_custom_target(IRLightingDemoAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_LIGHTING_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_LIGHTING_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_LIGHTING_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_LIGHTING_CONFIG_LUA_DST}
)

foreach(IR_LIGHTING_DEMO_TARGET IN LISTS IR_LIGHTING_DEMO_TARGETS)
    add_dependencies(${IR_LIGHTING_DEMO_TARGET} IRLightingDemoAssets)
endforeach()

add_custom_target(IRLightingDemos DEPENDS ${IR_LIGHTING_DEMO_TARGETS})
