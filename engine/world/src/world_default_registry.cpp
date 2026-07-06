#include <irreden/world/world_snapshot.hpp>

// Heavy include: the audited per-component save-policy table + every component
// type's definition. Confined to this one TU (never a widely-included header,
// per save_component_inventory.hpp's own note) so promoting the process-default
// registry to more components only recompiles this file.
#include <irreden/world/save_component_inventory.hpp>

// The SaveSerialize<C_VoxelSetNew> specialization (persist P6) — NOT pulled by
// the component header, so it must be in scope wherever a registry registers
// C_VoxelSetNew, else registerComponent instantiates the primary template and
// the static_assert (not trivially copyable) fires.
#include <irreden/voxel/voxel_set_serialize.hpp>

namespace IRWorld {

// The curated process-default registry (persist P7, #2218). Only components
// with a working SaveSerialize<C> today: C_VoxelSetNew (explicit serializer,
// P6) plus trivially-copyable plain-data components. Registering the full
// AllEngineComponents tuple does NOT compile — the heap-owning opted-in
// components (C_Name, C_Skeleton, C_MidiSequence, C_TextSegment, ...) have no
// SaveSerialize<C> yet and would hit the primary template's static_assert.
// Extend this list as each component grows a serializer; the full-inventory
// filter (a HasExplicitSaveSerialize<C> trait over AllEngineComponents) is
// deferred downstream work. See world_snapshot.hpp for the rationale.
SaveRegistry makeDefaultSaveRegistry() {
    SaveRegistry registry;
    registry.registerComponent<IRComponents::C_VoxelSetNew>();
    registry.registerComponent<IRComponents::C_LocalTransform>();
    registry.registerComponent<IRComponents::C_PositionInt3D>();
    registry.registerComponent<IRComponents::C_SizeInt3D>();
    return registry;
}

IRAsset::BinaryStatus saveWorld(const std::string &path) {
    return saveWorld(makeDefaultSaveRegistry(), path);
}

LoadResult loadWorld(const std::string &path) {
    return loadWorld(makeDefaultSaveRegistry(), path);
}

} // namespace IRWorld
