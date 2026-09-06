set(IR_ZYAW_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_ZYAW_RUNTIME_DIR})

set(IR_ZYAW_DEMO_TARGETS
    IRZYawStatic
    IRZYawInteractive
)

set(IR_ZYAW_DEMO_SOURCES
    main_static.cpp
    main_mouse.cpp
)

list(LENGTH IR_ZYAW_DEMO_TARGETS IR_ZYAW_DEMO_COUNT)
math(EXPR IR_ZYAW_DEMO_LAST "${IR_ZYAW_DEMO_COUNT} - 1")

foreach(IR_ZYAW_DEMO_INDEX RANGE ${IR_ZYAW_DEMO_LAST})
    list(GET IR_ZYAW_DEMO_TARGETS ${IR_ZYAW_DEMO_INDEX} IR_ZYAW_DEMO_TARGET)
    list(GET IR_ZYAW_DEMO_SOURCES ${IR_ZYAW_DEMO_INDEX} IR_ZYAW_DEMO_SOURCE)

    add_executable(${IR_ZYAW_DEMO_TARGET} ${IR_ZYAW_DEMO_SOURCE})
    target_link_libraries(${IR_ZYAW_DEMO_TARGET} PUBLIC IrredenEngine)
endforeach()

# Both demos share one runtime dir, so stage the asset DIRECTORIES ONCE (via
# the first target's bundle) and have every demo depend on that. Preserves the
# original single-IRZYawDemoAssets layout and avoids parallel copies into the
# same dir. Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
#
# The Windows DLL copy is NOT part of that shared staging: irreden_bundle_assets
# attaches it as a POST_BUILD step on the primary executable itself, which
# IRZYawInteractive never builds. Building it alone left it without its DLLs
# (#3084, same shape as #3075). So the non-primary demo gets its own
# irreden_stage_runtime_dlls call — a per-target POST_BUILD copy (`copy -t`,
# idempotent and cheap) — while the directory copies stay single-owner via the
# primary's Assets target.
list(GET IR_ZYAW_DEMO_TARGETS 0 IR_ZYAW_PRIMARY_TARGET)
irreden_bundle_assets(${IR_ZYAW_PRIMARY_TARGET} SCRIPTS config.lua)
foreach(IR_ZYAW_DEMO_TARGET IN LISTS IR_ZYAW_DEMO_TARGETS)
    add_dependencies(${IR_ZYAW_DEMO_TARGET} ${IR_ZYAW_PRIMARY_TARGET}Assets)
    if(NOT IR_ZYAW_DEMO_TARGET STREQUAL IR_ZYAW_PRIMARY_TARGET)
        irreden_stage_runtime_dlls(${IR_ZYAW_DEMO_TARGET})
    endif()
endforeach()

add_custom_target(IRZYawDemos DEPENDS ${IR_ZYAW_DEMO_TARGETS})

add_custom_target(IRZYawStaticRun
    COMMAND $<TARGET_FILE:IRZYawStatic>
    DEPENDS IRZYawStatic ${IR_ZYAW_PRIMARY_TARGET}Assets
    WORKING_DIRECTORY ${IR_ZYAW_RUNTIME_DIR}
    USES_TERMINAL
)

add_custom_target(IRZYawInteractiveRun
    COMMAND $<TARGET_FILE:IRZYawInteractive>
    DEPENDS IRZYawInteractive ${IR_ZYAW_PRIMARY_TARGET}Assets
    WORKING_DIRECTORY ${IR_ZYAW_RUNTIME_DIR}
    USES_TERMINAL
)
