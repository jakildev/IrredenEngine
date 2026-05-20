#ifndef IR_PREFAB_VOXEL_RIG_BRIDGE_H
#define IR_PREFAB_VOXEL_RIG_BRIDGE_H

// IRAsset::Rig ↔ IRComponents::C_JointHierarchy bridge — keeps engine/asset/ free of prefab dependencies.

#include <irreden/asset/rig_format.hpp>

#include <irreden/voxel/components/component_bind_points.hpp>
#include <irreden/voxel/components/component_joint_hierarchy.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

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

// Records with empty names are skipped; later duplicate names win.
inline IRComponents::C_BindPoints toBindPoints(const IRAsset::Rig &rig) {
    IRComponents::C_BindPoints bindPoints;
    bindPoints.points_.reserve(rig.bindPoints_.size());
    for (const auto &record : rig.bindPoints_) {
        if (record.name_.empty()) {
            continue;
        }
        IRComponents::BindPointRuntime runtime;
        runtime.boneId_ = record.boneId_;
        runtime.offset_ = record.offset_;
        runtime.rotation_ = record.rotation_;
        bindPoints.points_[record.name_] = runtime;
    }
    return bindPoints;
}

struct BindPointWorldTransform {
    IRMath::vec3 offset_{0.0f, 0.0f, 0.0f};
    IRMath::vec4 rotation_{0.0f, 0.0f, 0.0f, 1.0f};
};

// Walks chain root-first; (parent == current || parent >= size) sentinel stops malformed cycles.
inline BindPointWorldTransform worldTransformForBindPoint(
    const IRComponents::BindPointRuntime &point, const IRComponents::C_JointHierarchy &hierarchy
) {
    BindPointWorldTransform world;
    if (point.boneId_ >= hierarchy.joints_.size()) {
        world.offset_ = point.offset_;
        world.rotation_ = point.rotation_;
        return world;
    }

    IRMath::vec3 chainTranslation{0.0f, 0.0f, 0.0f};
    IRMath::vec4 chainRotation{0.0f, 0.0f, 0.0f, 1.0f};

    std::vector<std::uint32_t> chain;
    chain.reserve(hierarchy.joints_.size());
    std::uint32_t current = point.boneId_;
    const std::size_t maxDepth = hierarchy.joints_.size();
    for (std::size_t step = 0; step < maxDepth; ++step) {
        chain.push_back(current);
        const std::uint32_t parent = hierarchy.joints_[current].parentIndex_;
        if (parent == current || parent >= hierarchy.joints_.size()) {
            break;
        }
        current = parent;
    }

    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const auto &joint = hierarchy.joints_[*it];
        const IRMath::vec3 localTranslation{
            joint.translation_.x,
            joint.translation_.y,
            joint.translation_.z
        };
        const IRMath::vec3 rotatedLocal =
            IRMath::rotateVectorByQuat(localTranslation, chainRotation);
        chainTranslation = chainTranslation + rotatedLocal;
        chainRotation = IRMath::quatMul(chainRotation, joint.rotation_);
    }

    world.offset_ = chainTranslation + IRMath::rotateVectorByQuat(point.offset_, chainRotation);
    world.rotation_ = IRMath::quatMul(chainRotation, point.rotation_);
    return world;
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
