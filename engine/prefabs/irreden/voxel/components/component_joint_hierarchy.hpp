#ifndef COMPONENT_JOINT_HIERARCHY_H
#define COMPONENT_JOINT_HIERARCHY_H

// PURPOSE: Per-entity joint tree for skeletal-style procedural animation.
//   Each joint has a rotation quaternion (vec4), translation, and parent
//   index. Converts to GPUJointTransform for upload to the JointBuffer
//   SSBO (binding 21) consumed by the shapes compute shader.
// STATUS: WIP stub -- component defined with toGPUFormat() but:
//   - No system reads or writes joint data.
//   - system_shapes_to_trixel.hpp creates the JointTransformBuffer SSBO
//     but never uploads data; c_shapes_to_trixel.glsl declares JointBuffer
//     but main() never reads from it.
//   - system_shapes_to_trixel.hpp sets desc.jointIndex = 0 for all shapes.
// TODO:
//   1. Add JOINT_ANIMATION to SystemName enum.
//   2. Create a system (or extend SHAPES_TO_TRIXEL) that:
//      a. Uploads GPUJointTransform data per entity to JointTransformBuffer.
//      b. Sets desc.jointIndex per shape to the correct joint.
//   3. Update c_shapes_to_trixel.glsl main() to read joint transforms and
//      apply parent-chain rotation/translation to voxel positions.
//   4. Create a demo entity with joints (e.g. a 3-joint articulated arm
//      oscillating sinusoidally) to test the full pipeline.
// DEPENDENCIES: IRRender (GPUJointTransform), IRMath (vec4).

#include <irreden/ir_math.hpp>
#include <irreden/render/ir_render_types.hpp>

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

    std::vector<IRRender::GPUJointTransform> toGPUFormat() const {
        std::vector<IRRender::GPUJointTransform> gpu;
        gpu.reserve(joints_.size());
        for (const auto &j : joints_) {
            IRRender::GPUJointTransform t{};
            t.rotation = j.rotation_;
            t.translation = j.translation_;
            t.parentJointIndex = j.parentIndex_;
            gpu.push_back(t);
        }
        return gpu;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_JOINT_HIERARCHY_H */
