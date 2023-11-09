add_library(IrredenEngineEntity STATIC
    src/ir_entity.cpp
    src/archetype_graph.cpp
    src/archetype_node.cpp
    src/entity_manager.cpp
    src/archetype.cpp
)

target_compile_options(
    IrredenEngineEntity
    PUBLIC
    -O3
)

target_include_directories(
    IrredenEngineEntity PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)


target_link_libraries(
    IrredenEngineEntity PUBLIC
    IrredenEngineProfiling
    IrredenEngineMath
    # IrredenEngineInput
)