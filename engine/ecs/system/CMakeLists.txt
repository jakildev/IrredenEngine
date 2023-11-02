add_library(IrredenEngineSystem STATIC
    # src/entity.cpp
    # src/entity_handle.cpp
    # src/entity_manager.cpp
    # src/ir_ecs.cpp
    src/system_manager.cpp
    src/ir_system.cpp

#     include/irreden/ecs/entity.hpp
#     include/irreden/ecs/entity_handle.hpp
#     include/irreden/ecs/entity_manager.hpp
#     include/irreden/ecs/i_component_data.hpp
)

target_compile_options(
    IrredenEngineSystem
    PUBLIC
    -O3
)

target_include_directories(
    IrredenEngineSystem
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    IrredenEngineSystem PUBLIC
    IrredenEngineProfiling
    # IrredenEngineInput
    IrredenEngineEntity
    IrredenEngineTime
)