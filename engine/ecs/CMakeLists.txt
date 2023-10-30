add_library(IrredenEngineECS STATIC
    src/archetype_graph.cpp
    src/archetype_node.cpp
    src/entity_manager.cpp
    src/ir_ecs.cpp
    src/system_manager.cpp


    include/irreden/ir_ecs.hpp
    include/irreden/ecs/archetype_graph.hpp
    include/irreden/ecs/archetype_node.hpp
    include/irreden/ecs/entity_handle.hpp
    include/irreden/ecs/entity_manager.hpp
    include/irreden/ecs/i_component_data.hpp
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
    IrredenEngineProfiling
    IrredenEngineInput
)

