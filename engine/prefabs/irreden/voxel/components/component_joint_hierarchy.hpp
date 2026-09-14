#ifndef COMPONENT_JOINT_HIERARCHY_H
#define COMPONENT_JOINT_HIERARCHY_H

// DEPRECATED — superseded by the entity-based joint model.
//
// New rigs use:
//   - `C_Skeleton` (rig root, holds an ordered vector of joint EntityIds).
//     See `component_skeleton.hpp`.
//   - `C_Joint` tag on each joint entity. See `component_joint.hpp`.
//   - The engine's canonical local-transform component (`C_LocalTransform`)
//     for per-joint local transform.
//   - `CHILD_OF` relations for the parent chain.
//
// The `setRotation` / `setTranslation` setters here have no equivalent in
// the entity-based model: pose authoring writes the per-joint local-
// transform component directly, then `SYSTEM_PROPAGATE_TRANSFORM` walks the
// CHILD_OF chain to produce world transforms. See
// `engine/prefabs/irreden/voxel/CLAUDE.md` "Entity-based joints".
//
// This header remains for one release as a deprecation shim so existing
// callers compile while the consumer migration lands. Do not add new code
// that depends on it.

#include <irreden/ir_math.hpp>

#include <vector>

using namespace IRMath;

namespace IRComponents {

struct Joint {
    vec4 rotation_ = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    vec4 translation_ = vec4(0.0f);
    std::uint32_t parentIndex_ = 0;
};

struct C_JointHierarchy {
    std::vector<Joint> joints_;

    C_JointHierarchy() = default;

    std::uint32_t addJoint(vec4 translation, std::uint32_t parentIndex = 0) {
        Joint j;
        j.translation_ = translation;
        j.parentIndex_ = parentIndex;
        joints_.push_back(j);
        return static_cast<std::uint32_t>(joints_.size() - 1);
    }

    void setRotation(std::uint32_t jointIndex, vec4 rotation) {
        if (jointIndex < joints_.size()) {
            joints_[jointIndex].rotation_ = rotation;
        }
    }

    void setTranslation(std::uint32_t jointIndex, vec4 translation) {
        if (jointIndex < joints_.size()) {
            joints_[jointIndex].translation_ = translation;
        }
    }
};

} // namespace IRComponents

#endif /* COMPONENT_JOINT_HIERARCHY_H */
