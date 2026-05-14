#ifndef IR_PREFAB_VOXEL_RIG_BRIDGE_H
#define IR_PREFAB_VOXEL_RIG_BRIDGE_H

/// Translate between the asset-side `IRAsset::Rig` (on-disk, designer-
/// facing — carries per-joint names) and the runtime
/// `IRComponents::C_JointHierarchy` (ECS-resident, GPU-feeding — no names
/// because names are not needed at draw time). The split keeps
/// `engine/asset/` independent of the prefab voxel component (asset has
/// no dependency on prefabs) while still letting an editor or load path
/// produce a `C_JointHierarchy` directly from a `.rig` round-trip.
///
/// Pattern is the same shape as `engine/prefabs/irreden/render/fog_of_war.hpp`
/// (`engine/prefabs/CLAUDE.md` §"Component method rules", "Prefab-scoped
/// namespace") — the bridge owns the cross-type translation logic so
/// callers don't have to hand-roll it.

#include <irreden/asset/rig_format.hpp>

#include <irreden/voxel/components/component_joint_hierarchy.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <utility>

namespace IRPrefab::Rig {

/// Build a runtime `C_JointHierarchy` from an asset `IRAsset::Rig`.
/// Joint order is preserved; names are dropped (the runtime component
/// has no name field — see `component_joint_hierarchy.hpp`).
inline IRComponents::C_JointHierarchy toComponent(const IRAsset::Rig &rig) {
    IRComponents::C_JointHierarchy hierarchy;
    hierarchy.joints_.reserve(rig.joints_.size());
    for (const auto &assetJoint : rig.joints_) {
        IRComponents::Joint runtime;
        runtime.rotation_ = assetJoint.rotation_;
        runtime.translation_ = assetJoint.translation_;
        runtime.parentIndex_ = assetJoint.parentIndex_;
        hierarchy.joints_.push_back(runtime);
    }
    return hierarchy;
}

/// Build an asset `IRAsset::Rig` from a runtime `C_JointHierarchy`.
/// @p jointNames is paired by index; pass an empty span to leave every
/// joint name blank. Names shorter than the joint count default the
/// remaining names to empty strings, so partial-name authoring
/// (root-named, children anonymous) costs nothing.
inline IRAsset::Rig fromComponent(
    const IRComponents::C_JointHierarchy &hierarchy, std::span<const std::string> jointNames = {}
) {
    IRAsset::Rig rig;
    rig.joints_.reserve(hierarchy.joints_.size());
    for (std::size_t i = 0; i < hierarchy.joints_.size(); ++i) {
        IRAsset::RigJoint assetJoint;
        assetJoint.rotation_ = hierarchy.joints_[i].rotation_;
        assetJoint.translation_ = hierarchy.joints_[i].translation_;
        assetJoint.parentIndex_ = hierarchy.joints_[i].parentIndex_;
        if (i < jointNames.size()) {
            assetJoint.name_ = jointNames[i];
        }
        rig.joints_.push_back(std::move(assetJoint));
    }
    return rig;
}

} // namespace IRPrefab::Rig

#endif /* IR_PREFAB_VOXEL_RIG_BRIDGE_H */
