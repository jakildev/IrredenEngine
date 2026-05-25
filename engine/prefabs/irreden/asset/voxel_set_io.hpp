#ifndef IR_PREFAB_ASSET_VOXEL_SET_IO_H
#define IR_PREFAB_ASSET_VOXEL_SET_IO_H

/// `C_ShapeDescriptor`-aware adapter over the low-level
/// `engine/asset/include/irreden/asset/voxel_set_format.hpp` API.
///
/// Lives in `engine/prefabs/` rather than `engine/asset/` because
/// `engine/asset/` does not (and should not) depend on the prefab
/// components — the layer map keeps engine/asset below render and
/// entity. Callers that need the descriptor-shaped save/load surface
/// include this header; callers working with raw `ShapeRecord` arrays
/// can use the lower-level API directly.

#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace IRAsset {

/// Persist a shape-group `.vxs` from a span of `C_ShapeDescriptor` plus
/// parallel arrays for the runtime-only metadata the descriptor does
/// not carry (offset / rotation / CSG op / bone-id binding). The
/// underlying `saveShapeGroup` also emits a `.vxs.json` sidecar
/// (Rule #6) on a successful binary write.
///
/// Empty parallel spans take the per-record default (identity
/// transform, `CsgOp::NONE`, `boneId = 0`). Non-empty spans should
/// match `descriptors.size()` — mismatched lengths log a warning and
/// degrade gracefully by stopping at the shorter span, not UB.
///
/// @deprecated SHAPES write path retired per Epic D D2 (#960). New `.vxs`
/// assets must use `saveDenseVoxelSet`. See `docs/design/sdf-migration-plan.md`.
inline BinaryStatus saveVoxelSet(
    const std::string &path,
    std::span<const IRComponents::C_ShapeDescriptor> descriptors,
    std::span<const vec3> offsets = {},
    std::span<const vec4> rotations = {},
    std::span<const CsgOp> csgOps = {},
    std::span<const std::uint8_t> boneIds = {}
) {
    if (!offsets.empty() && offsets.size() != descriptors.size()) {
        IRE_LOG_WARN(
            "saveVoxelSet: offsets.size()={} != descriptors.size()={}",
            offsets.size(),
            descriptors.size()
        );
    }
    if (!rotations.empty() && rotations.size() != descriptors.size()) {
        IRE_LOG_WARN(
            "saveVoxelSet: rotations.size()={} != descriptors.size()={}",
            rotations.size(),
            descriptors.size()
        );
    }
    if (!csgOps.empty() && csgOps.size() != descriptors.size()) {
        IRE_LOG_WARN(
            "saveVoxelSet: csgOps.size()={} != descriptors.size()={}",
            csgOps.size(),
            descriptors.size()
        );
    }
    if (!boneIds.empty() && boneIds.size() != descriptors.size()) {
        IRE_LOG_WARN(
            "saveVoxelSet: boneIds.size()={} != descriptors.size()={}",
            boneIds.size(),
            descriptors.size()
        );
    }

    std::vector<ShapeRecord> records;
    records.reserve(descriptors.size());
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const auto &d = descriptors[i];
        ShapeRecord r;
        r.shapeTypeId_ = static_cast<std::uint32_t>(d.shapeType_);
        r.params_ = d.params_;
        r.color_ = d.color_;
        r.flags_ = d.flags_;
        if (i < offsets.size())
            r.offset_ = offsets[i];
        if (i < rotations.size())
            r.rotation_ = rotations[i];
        if (i < csgOps.size())
            r.csgOp_ = csgOps[i];
        if (i < boneIds.size())
            r.boneId_ = boneIds[i];
        records.push_back(r);
    }
    return saveShapeGroup(path, records);
}

} // namespace IRAsset

#endif /* IR_PREFAB_ASSET_VOXEL_SET_IO_H */
