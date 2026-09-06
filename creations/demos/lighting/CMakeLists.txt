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
    IRLightingSdfBlocker
    IRLightingSunOrbit
    IRLightingSunElevationOrbit
    IRLightingPerCanvasScope
    IRLightingHDR
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
    main_sdf_blocker.cpp
    main_sun_orbit.cpp
    main_sun_elevation_orbit.cpp
    main_per_canvas_scope.cpp
    main_hdr.cpp
)

list(LENGTH IR_LIGHTING_DEMO_TARGETS IR_LIGHTING_DEMO_COUNT)
math(EXPR IR_LIGHTING_DEMO_LAST "${IR_LIGHTING_DEMO_COUNT} - 1")

foreach(IR_LIGHTING_DEMO_INDEX RANGE ${IR_LIGHTING_DEMO_LAST})
    list(GET IR_LIGHTING_DEMO_TARGETS ${IR_LIGHTING_DEMO_INDEX} IR_LIGHTING_DEMO_TARGET)
    list(GET IR_LIGHTING_DEMO_SOURCES ${IR_LIGHTING_DEMO_INDEX} IR_LIGHTING_DEMO_SOURCE)

    add_executable(${IR_LIGHTING_DEMO_TARGET} ${IR_LIGHTING_DEMO_SOURCE})
    target_link_libraries(${IR_LIGHTING_DEMO_TARGET} PUBLIC IrredenEngine)
    target_include_directories(${IR_LIGHTING_DEMO_TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()

# All lighting demos share one runtime dir, so stage the asset DIRECTORIES
# ONCE (via the first target's bundle) and have every demo depend on that.
# Preserves the original single-IRLightingDemoAssets layout and avoids N
# copy_directory jobs racing into the same dir under make -j.
# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
#
# The Windows DLL copy is NOT part of that shared staging: irreden_bundle_assets
# attaches it as a POST_BUILD step on the primary executable itself, which the
# other 15 demos never build. Building one of those alone left it without its
# DLLs (#3075). So each non-primary demo gets its own irreden_stage_runtime_dlls
# call — a per-target POST_BUILD copy (`copy -t`, idempotent and cheap) — while
# the directory copies stay single-owner via the primary's Assets target.
list(GET IR_LIGHTING_DEMO_TARGETS 0 IR_LIGHTING_PRIMARY_TARGET)
irreden_bundle_assets(${IR_LIGHTING_PRIMARY_TARGET} SCRIPTS config.lua)
foreach(IR_LIGHTING_DEMO_TARGET IN LISTS IR_LIGHTING_DEMO_TARGETS)
    add_dependencies(${IR_LIGHTING_DEMO_TARGET} ${IR_LIGHTING_PRIMARY_TARGET}Assets)
    if(NOT IR_LIGHTING_DEMO_TARGET STREQUAL IR_LIGHTING_PRIMARY_TARGET)
        irreden_stage_runtime_dlls(${IR_LIGHTING_DEMO_TARGET})
    endif()
endforeach()

add_custom_target(IRLightingDemos DEPENDS ${IR_LIGHTING_DEMO_TARGETS})
