#ifndef COMPONENT_JOINT_HIERARCHY_H
#define COMPONENT_JOINT_HIERARCHY_H

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
