#ifndef COMPONENT_JOINT_HIERARCHY_H
#define COMPONENT_JOINT_HIERARCHY_H

// DEPRECATED — superseded by the entity-based joint model.
//
// New rigs should use:
//   - `C_Skeleton` (rig root, holds an ordered vector of joint EntityIds).
//     See `component_skeleton.hpp`.
//   - `C_Joint` tag on each joint entity. See `component_joint.hpp`.
//   - Per-joint local transform via the engine's canonical local-transform
//     component (`C_Position3D` + `C_Rotation` today; `C_LocalTransform`
//     once #731 Phase 1 lands).
//   - `CHILD_OF` relations for the parent chain.
//
// The original SoA `C_JointHierarchy` packed every joint into one vector on
// the rig root. The entity-based model unlocks severance, per-joint custom
// components, dynamic re-parenting, and uniform reuse of the engine's
// `CHILD_OF` traversal — see `engine/prefabs/irreden/voxel/CLAUDE.md`
// "Entity-based joints" and the design refinement in `#737`.
//
// MIGRATION (#605 Phase 2 will do the consumer-side work):
//   - The GPU joint-matrix SSBO uploader reads `C_Skeleton.joints_` on each
//     rig root and packs per-joint world transforms (from
//     `C_WorldTransform`) at the matching slot, indexed by `C_Voxel.bone_id_`.
//   - The `setRotation` / `setTranslation` setters here have no equivalent —
//     pose authoring writes the per-joint local-transform component
//     directly, then `SYSTEM_PROPAGATE_TRANSFORM` walks the CHILD_OF chain
//     to produce world transforms.
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
