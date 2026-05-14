#ifndef IR_PREFAB_VOXEL_JOINT_HIERARCHY_GPU_H
#define IR_PREFAB_VOXEL_JOINT_HIERARCHY_GPU_H

// GPU-format bridge for C_JointHierarchy; include only in files that link IrredenEngineRendering.

#include <irreden/voxel/components/component_joint_hierarchy.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <vector>

namespace IRPrefab::Rig {

inline std::vector<IRRender::GPUJointTransform>
toGPUFormat(const IRComponents::C_JointHierarchy &h) {
    std::vector<IRRender::GPUJointTransform> gpu;
    gpu.reserve(h.joints_.size());
    for (const auto &j : h.joints_) {
        IRRender::GPUJointTransform t{};
        t.rotation = j.rotation_;
        t.translation = j.translation_;
        t.parentJointIndex = j.parentIndex_;
        gpu.push_back(t);
    }
    return gpu;
}

} // namespace IRPrefab::Rig

#endif /* IR_PREFAB_VOXEL_JOINT_HIERARCHY_GPU_H */
