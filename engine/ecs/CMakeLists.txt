add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/entity)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/system)

add_library(IrredenEngineECS STATIC
    src/ir_ecs.cpp
)

target_compile_options(
    IrredenEngineECS
    PUBLIC
    -O3
)

target_include_directories(
    IrredenEngineECS PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    IrredenEngineECS PUBLIC
    IrredenEngineProfile
    # IrredenEngineInput
    IrredenEngineEntity
    IrredenEngineSystem
)

