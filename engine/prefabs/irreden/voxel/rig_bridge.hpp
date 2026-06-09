#ifndef IR_PREFAB_VOXEL_RIG_BRIDGE_H
#define IR_PREFAB_VOXEL_RIG_BRIDGE_H

// IRAsset::Rig ↔ IRComponents::C_JointHierarchy bridge — keeps engine/asset/ free of prefab
// dependencies.

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

namespace detail {

// Fills `chainOut` (cleared first) with the joint indices from `start` up to
// the root, leaf-first / root-last. The (parent == current || parent >=
// jointCount) sentinel stops a malformed chain instead of looping. `parentOf`
// returns joint i's parentIndex_; the two callers hold different joint
// containers (IRAsset::Rig vs C_JointHierarchy), so the accessor is a template
// parameter rather than a fixed type. Caller-owned buffer so a per-joint loop
// (bindPose) reuses one allocation across the whole skeleton.
template <typename ParentOf>
inline void collectJointChainToRoot(
    std::uint32_t start,
    std::size_t jointCount,
    ParentOf parentOf,
    std::vector<std::uint32_t> &chainOut
) {
    chainOut.clear();
    std::uint32_t current = start;
    for (std::size_t step = 0; step < jointCount; ++step) {
        chainOut.push_back(current);
        const std::uint32_t parent = parentOf(current);
        if (parent == current || parent >= jointCount) {
            break;
        }
        current = parent;
    }
}

} // namespace detail

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
    detail::collectJointChainToRoot(
        point.boneId_,
        hierarchy.joints_.size(),
        [&](std::uint32_t i) { return hierarchy.joints_[i].parentIndex_; },
        chain
    );

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

// Per-joint bind (rest) pose in rig-root-local space — one IRMath::SQT per
// joint, in `joints_` order — exactly what populates C_Skeleton.bindPose_.
// Each entry is joint i's JNTS local rest transform composed up its parent
// chain with sqtCompose (the PROPAGATE_TRANSFORM convention), so a joint left
// at rest has C_WorldTransform == bindPose[i] and IRPrefab::Skeleton::skinMatrix
// returns identity at the bind pose.
//
// Source is the JNTS chunk, NOT the `.rig` BIND chunk: BIND stores named
// attachment points (toBindPoints / C_BindPoints), a separate concept from the
// per-joint skinning bind pose despite the chunk name. Shares the
// detail::collectJointChainToRoot walk with worldTransformForBindPoint.
inline std::vector<IRMath::SQT> bindPose(const IRAsset::Rig &rig) {
    const std::size_t jointCount = rig.joints_.size();
    std::vector<IRMath::SQT> pose(jointCount);

    std::vector<std::uint32_t> chain;
    chain.reserve(jointCount);
    for (std::size_t j = 0; j < jointCount; ++j) {
        detail::collectJointChainToRoot(
            static_cast<std::uint32_t>(j),
            jointCount,
            [&](std::uint32_t i) { return rig.joints_[i].parentIndex_; },
            chain
        );

        IRMath::SQT world; // identity
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const auto &assetJoint = rig.joints_[*it];
            IRMath::SQT local;
            local.rotation_ = assetJoint.rotation_;
            local.translation_ = IRMath::vec3(
                assetJoint.translation_.x,
                assetJoint.translation_.y,
                assetJoint.translation_.z
            );
            world = IRMath::sqtCompose(world, local);
        }
        pose[j] = world;
    }
    return pose;
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
