#ifndef IR_PREFAB_VOXEL_DENSE_BRIDGE_H
#define IR_PREFAB_VOXEL_DENSE_BRIDGE_H

/// Translate between the asset-side `IRAsset::DenseVoxelSet` (on-disk
/// per-voxel records, designer-facing — carries layers, frames, meta)
/// and the runtime `IRComponents::C_VoxelSetNew` (ECS-resident,
/// canvas-pool-backed or headless-staged). The split mirrors
/// `voxel/rig_bridge.hpp` for joints: `engine/asset/` stays free of
/// the prefab voxel component, and the bridge owns the cross-type
/// translation so callers don't reach into either side's private
/// representation.
///
/// `IRAsset::VoxelRecord` and `IRComponents::C_Voxel` share an
/// identical 12-byte std430 layout (asserted at the asset-format
/// header), but they are distinct types so the bridge does a
/// per-field copy. The cost is irrelevant at prefab-spawn rate;
/// in exchange the boundary stays explicit and a future divergence
/// in either layout would show up as a compile error.

#include <irreden/asset/voxel_set_format.hpp>

#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstddef>
#include <vector>

namespace IRPrefab::DenseVoxel {

/// Translate an asset `IRAsset::DenseVoxelSet` into plain runtime
/// `C_Voxel` records — the CPU-side half of `toComponent`, with no pool
/// involvement.
///
/// Reach for this over `toComponent` whenever a caller holds more dense
/// sets than it means to make ECS-resident. `C_VoxelSetNew` reserves a
/// canvas voxel-pool span in its ctor and gives it back only in
/// `onDestroy()`, which the ECS calls for a component an entity owns
/// (`i_component_data.hpp`); there is no destructor, so a stack-local
/// built once a canvas exists strands its span for the process lifetime.
/// An N-frame animation is the shape that makes this bite: take
/// `toVoxels` for every frame, then build one `C_VoxelSetNew` from the
/// frame the entity actually holds.
///
/// Face-occlusion bits are whatever the asset carried; every consumer
/// that seeds or overwrites a pool span recomputes them (`seedIntoPool`,
/// `resyncAfterRawEdits`), so callers owe no recompute here.
///
/// Returns empty when `dense.voxels_.size() != dense.voxelCount()` (e.g.
/// malformed `.vxs` with BNDS present but VOXR truncated — Save Format
/// Extensibility Rule #5: unknown is recoverable), the same guard
/// `toComponent` surfaces as `recordCount() == 0`.
inline std::vector<IRComponents::C_Voxel> toVoxels(const IRAsset::DenseVoxelSet &dense) {
    const std::size_t expected = dense.voxelCount();
    if (expected == 0 || dense.voxels_.size() != expected) {
        return {};
    }

    std::vector<IRComponents::C_Voxel> runtimeVoxels;
    runtimeVoxels.reserve(expected);
    for (const auto &record : dense.voxels_) {
        runtimeVoxels.emplace_back(
            record.color_,
            record.material_id_,
            record.flags_,
            record.bone_id_,
            record.layer_id_
        );
    }
    return runtimeVoxels;
}

/// Build a runtime `C_VoxelSetNew` from an asset `IRAsset::DenseVoxelSet`.
/// When a render canvas is active the component allocates from its
/// pool and seeds positions / voxels in one pass; without a canvas the
/// data is staged in `pendingVoxels_` for a future seed step (see
/// `component_voxel_set.hpp` and `engine/prefabs/irreden/voxel/CLAUDE.md`).
///
/// Because the returned component owns a pool span the moment a canvas
/// exists, call this exactly once per set an entity will hold;
/// `toVoxels` is the multi-set / animation case.
///
/// If `dense.voxels_.size() != dense.voxelCount()` (e.g. malformed
/// `.vxs` with BNDS present but VOXR truncated — Save Format
/// Extensibility Rule #5: unknown is recoverable), the returned
/// component is empty (`recordCount() == 0`) so the caller can
/// surface a diagnostic without crashing.
inline IRComponents::C_VoxelSetNew toComponent(const IRAsset::DenseVoxelSet &dense) {
    const std::vector<IRComponents::C_Voxel> runtimeVoxels = toVoxels(dense);
    if (runtimeVoxels.empty()) {
        // Empty data path: route through the dense ctor (which is
        // headless-safe) with an empty span instead of the legacy
        // default ctor that asserts on an absent render manager.
        return IRComponents::C_VoxelSetNew{
            IRMath::ivec3(0), IRMath::ivec3(0), std::span<const IRComponents::C_Voxel>{}
        };
    }

    return IRComponents::C_VoxelSetNew{
        dense.boundsMin_, dense.boundsMax_, std::span<const IRComponents::C_Voxel>{runtimeVoxels}
    };
}

} // namespace IRPrefab::DenseVoxel

#endif /* IR_PREFAB_VOXEL_DENSE_BRIDGE_H */
