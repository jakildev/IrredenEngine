add_library(IrredenEngineEntity STATIC
    src/ir_entity.cpp
    src/archetype_graph.cpp
    src/archetype_node.cpp
    src/entity_manager.cpp
    src/archetype.cpp
    # src/ir_ecs.cpp


    # include/irreden/ir_ecs.hpp
    # include/irreden/ecs/archetype_graph.hpp
    # include/irreden/ecs/archetype_node.hpp
    # include/irreden/ecs/entity_handle.hpp
    # include/irreden/ecs/entity_manager.hpp
    # include/irreden/ecs/i_component_data.hpp
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
    IrredenEngineInput
)