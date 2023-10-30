add_library(IrredenEngineUpdate STATIC
    include/irreden/update/systems/system_acceleration.hpp
    include/irreden/update/systems/system_creator_chunk.hpp
    include/irreden/update/systems/system_creator_triangles_only_entity.hpp
    include/irreden/update/systems/system_goto_3d.hpp
    include/irreden/update/systems/system_gravity.hpp
    include/irreden/update/systems/system_lifetime.hpp
    include/irreden/update/systems/system_particle_spawner.hpp
    include/irreden/update/systems/system_periodic_idle.hpp
    include/irreden/update/systems/system_update_voxel_set_children.hpp
    include/irreden/update/systems/system_velocity.hpp
    include/irreden/update/systems/system_voxel_pool.hpp
    include/irreden/update/systems/system_voxel_set_reshaper.hpp

    src/system_creator_chunk.cpp
    src/system_creator_triangles_only_entity.cpp
)

include_directories(
    IrredenEngineUpdate
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)