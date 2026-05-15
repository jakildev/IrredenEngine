#ifndef IR_PREFAB_VOXEL_RIG_BRIDGE_H
#define IR_PREFAB_VOXEL_RIG_BRIDGE_H

// IRAsset::Rig ↔ IRComponents::C_JointHierarchy bridge — keeps engine/asset/ free of prefab dependencies.

#include <irreden/asset/rig_format.hpp>

#include <irreden/voxel/components/component_joint_hierarchy.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <utility>

namespace IRPrefab::Rig {

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
